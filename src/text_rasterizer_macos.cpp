#include "text_rasterizer.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace wipreview::text {
namespace {

template <typename T>
struct CFReleaser {
  void operator()(T value) const noexcept {
    if (value) CFRelease(value);
  }
};

template <typename T>
using CFPtr = std::unique_ptr<std::remove_pointer_t<T>, CFReleaser<T>>;

std::string cfStringToUTF8(CFStringRef value) {
  if (!value) return {};
  const CFIndex length = CFStringGetLength(value);
  const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  if (capacity <= 1) return {};
  std::string result(static_cast<std::size_t>(capacity), '\0');
  if (!CFStringGetCString(value, result.data(), capacity, kCFStringEncodingUTF8)) return {};
  result.resize(std::strlen(result.c_str()));
  return result;
}

CTFontSymbolicTraits traitsFor(FontStyle style) noexcept {
  switch (style) {
    case FontStyle::Regular: return 0;
    case FontStyle::Bold: return kCTFontBoldTrait;
    case FontStyle::Italic: return kCTFontItalicTrait;
    case FontStyle::BoldItalic: return kCTFontBoldTrait | kCTFontItalicTrait;
  }
  return 0;
}

CTFontRef createSystemFont(CGFloat size) noexcept {
  return CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
}

}  // namespace

GlyphMask rasterizeUTF8(const std::string& text, const std::string& fontFamily,
                        FontStyle style, double pixelSize) noexcept {
  GlyphMask result;
  try {
    if (text.empty() || text.size() > 16384 || !std::isfinite(pixelSize)) return result;
    const CGFloat size = static_cast<CGFloat>(std::clamp(pixelSize, 1.0, 4096.0));
    CFPtr<CFStringRef> string(CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false));
    if (!string) return result;

    CTFontRef rawBase = nullptr;
    if (!fontFamily.empty() && fontFamily != "System Default") {
      CFPtr<CFStringRef> requested(CFStringCreateWithCString(
          kCFAllocatorDefault, fontFamily.c_str(), kCFStringEncodingUTF8));
      if (requested) rawBase = CTFontCreateWithName(requested.get(), size, nullptr);
    }
    if (!rawBase) {
      rawBase = createSystemFont(size);
      result.usedFallback = !fontFamily.empty() && fontFamily != "System Default";
    }
    CFPtr<CTFontRef> base(rawBase);
    if (!base) return result;

    CFPtr<CTFontRef> styled;
    const CTFontSymbolicTraits traits = traitsFor(style);
    if (traits != 0) {
      styled.reset(CTFontCreateCopyWithSymbolicTraits(base.get(), size, nullptr, traits, traits));
      if (!styled) result.usedFallback = true;
    }
    CTFontRef font = styled ? styled.get() : base.get();
    CFPtr<CFStringRef> postScriptName(CTFontCopyPostScriptName(font));
    result.resolvedFont = cfStringToUTF8(postScriptName.get());

    const void* keys[] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
    const void* values[] = {font, kCFBooleanTrue};
    CFPtr<CFDictionaryRef> attributes(CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    if (!attributes) return {};
    CFPtr<CFAttributedStringRef> attributed(CFAttributedStringCreate(
        kCFAllocatorDefault, string.get(), attributes.get()));
    if (!attributed) return {};
    CFPtr<CTLineRef> line(CTLineCreateWithAttributedString(attributed.get()));
    if (!line) return {};

    CGRect bounds = CTLineGetBoundsWithOptions(line.get(), kCTLineBoundsUseGlyphPathBounds);
    if (CGRectIsNull(bounds) || CGRectIsEmpty(bounds)) return result;
    const int minX = static_cast<int>(std::floor(CGRectGetMinX(bounds))) - 2;
    const int minY = static_cast<int>(std::floor(CGRectGetMinY(bounds))) - 2;
    const int maxX = static_cast<int>(std::ceil(CGRectGetMaxX(bounds))) + 2;
    const int maxY = static_cast<int>(std::ceil(CGRectGetMaxY(bounds))) + 2;
    const int width = maxX - minX;
    const int height = maxY - minY;
    if (width <= 0 || height <= 0 || width > 65536 || height > 8192) return {};
    const std::size_t byteCount = static_cast<std::size_t>(width)
                                * static_cast<std::size_t>(height);
    if (byteCount > 256U * 1024U * 1024U) return {};
    std::vector<std::uint8_t> raw(byteCount, 0);

    CFPtr<CGColorSpaceRef> colourSpace(CGColorSpaceCreateDeviceGray());
    if (!colourSpace) return {};
    CFPtr<CGContextRef> context(CGBitmapContextCreate(
        raw.data(), static_cast<std::size_t>(width), static_cast<std::size_t>(height),
        8, static_cast<std::size_t>(width), colourSpace.get(), kCGImageAlphaNone));
    if (!context) return {};
    CGContextSetShouldAntialias(context.get(), true);
    CGContextSetShouldSmoothFonts(context.get(), true);
    CGContextSetGrayFillColor(context.get(), 1.0, 1.0);
    CGContextSetTextDrawingMode(context.get(), kCGTextFill);
    CGContextSetTextPosition(context.get(), static_cast<CGFloat>(-minX),
                             static_cast<CGFloat>(-minY));
    CTLineDraw(line.get(), context.get());

    int cropX1 = width;
    int cropY1 = height;
    int cropX2 = 0;
    int cropY2 = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (raw[static_cast<std::size_t>(y * width + x)] == 0) continue;
        cropX1 = std::min(cropX1, x);
        cropY1 = std::min(cropY1, y);
        cropX2 = std::max(cropX2, x + 1);
        cropY2 = std::max(cropY2, y + 1);
      }
    }
    if (cropX1 >= cropX2 || cropY1 >= cropY2) return {};
    result.width = cropX2 - cropX1;
    result.height = cropY2 - cropY1;
    result.pixels.resize(static_cast<std::size_t>(result.width)
                       * static_cast<std::size_t>(result.height));
    for (int y = 0; y < result.height; ++y) {
      std::memcpy(result.pixels.data() + static_cast<std::size_t>(y * result.width),
                  raw.data() + static_cast<std::size_t>((y + cropY1) * width + cropX1),
                  static_cast<std::size_t>(result.width));
    }
    return result;
  } catch (...) {
    return {};
  }
}

}  // namespace wipreview::text
