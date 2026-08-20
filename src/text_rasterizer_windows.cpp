#include "text_rasterizer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwrite.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace wipreview::text {
namespace {

template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ~ComPtr() { reset(); }
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  T* get() const noexcept { return value_; }
  T** put() noexcept {
    reset();
    return &value_;
  }
  T* operator->() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ != nullptr; }
  void reset() noexcept {
    if (value_) value_->Release();
    value_ = nullptr;
  }

 private:
  T* value_ = nullptr;
};

std::wstring utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), result.data(), length) != length) {
    return {};
  }
  return result;
}

DWRITE_FONT_WEIGHT fontWeight(FontStyle style) noexcept {
  return style == FontStyle::Bold || style == FontStyle::BoldItalic
      ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
}

DWRITE_FONT_STYLE fontStyle(FontStyle style) noexcept {
  return style == FontStyle::Italic || style == FontStyle::BoldItalic
      ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

class BitmapTextRenderer final : public IDWriteTextRenderer {
 public:
  BitmapTextRenderer(IDWriteBitmapRenderTarget* target,
                     IDWriteRenderingParams* renderingParams) noexcept
      : target_(target), renderingParams_(renderingParams) {
    target_->AddRef();
    renderingParams_->AddRef();
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(
      REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) ||
        iid == __uuidof(IDWriteTextRenderer)) {
      *object = static_cast<IDWriteTextRenderer*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(
      void*, BOOL* disabled) override {
    if (!disabled) return E_POINTER;
    *disabled = FALSE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetCurrentTransform(
      void*, DWRITE_MATRIX* transform) override {
    return transform ? target_->GetCurrentTransform(transform) : E_POINTER;
  }

  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(
      void*, FLOAT* pixelsPerDip) override {
    return pixelsPerDip ? target_->GetPixelsPerDip(pixelsPerDip) : E_POINTER;
  }

  HRESULT STDMETHODCALLTYPE DrawGlyphRun(
      void*, FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_MEASURING_MODE measuringMode,
      const DWRITE_GLYPH_RUN* glyphRun,
      const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
    return target_->DrawGlyphRun(
        baselineOriginX, baselineOriginY, measuringMode, glyphRun,
        renderingParams_, RGB(255, 255, 255), nullptr);
  }

  HRESULT STDMETHODCALLTYPE DrawUnderline(
      void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawStrikethrough(
      void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawInlineObject(
      void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL,
      IUnknown*) override {
    return E_NOTIMPL;
  }

 private:
  ~BitmapTextRenderer() {
    renderingParams_->Release();
    target_->Release();
  }

  std::atomic<ULONG> references_{1};
  IDWriteBitmapRenderTarget* target_ = nullptr;
  IDWriteRenderingParams* renderingParams_ = nullptr;
};

}  // namespace

GlyphRaster rasterizeUTF8(const std::string& text,
                          const std::string& fontFamily,
                          FontStyle style,
                          double pixelSize) noexcept {
  GlyphRaster result;
  try {
    if (text.empty() || text.size() > 16384 || !std::isfinite(pixelSize)) {
      return result;
    }
    const auto wideText = utf8ToWide(text);
    if (wideText.empty()) return result;

    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.put())))) {
      return result;
    }

    std::wstring family = fontFamily.empty() || fontFamily == "System Default"
        ? L"Segoe UI" : utf8ToWide(fontFamily);
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(factory->GetSystemFontCollection(fonts.put(), FALSE))) {
      return result;
    }
    UINT32 familyIndex = 0;
    BOOL familyExists = FALSE;
    if (family.empty() || FAILED(fonts->FindFamilyName(
            family.c_str(), &familyIndex, &familyExists)) || !familyExists) {
      family = L"Segoe UI";
      result.usedFallback = true;
    }

    const FLOAT size = static_cast<FLOAT>(
        std::clamp(pixelSize, 1.0, 4096.0));
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory->CreateTextFormat(
            family.c_str(), fonts.get(), fontWeight(style), fontStyle(style),
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", format.put()))) {
      return result;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(
            wideText.data(), static_cast<UINT32>(wideText.size()),
            format.get(), 65536.0F, 8192.0F, layout.put()))) {
      return result;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return result;
    constexpr int padding = 4;
    const int width = static_cast<int>(std::ceil(
        std::max(metrics.width, metrics.widthIncludingTrailingWhitespace))) +
        padding * 2;
    const int height = static_cast<int>(std::ceil(metrics.height)) + padding * 2;
    if (width <= 0 || height <= 0 || width > 65536 || height > 8192) {
      return result;
    }

    ComPtr<IDWriteGdiInterop> interop;
    if (FAILED(factory->GetGdiInterop(interop.put()))) return result;
    ComPtr<IDWriteBitmapRenderTarget> target;
    if (FAILED(interop->CreateBitmapRenderTarget(
            nullptr, static_cast<UINT32>(width),
            static_cast<UINT32>(height), target.put()))) {
      return result;
    }
    PatBlt(target->GetMemoryDC(), 0, 0, width, height, BLACKNESS);

    ComPtr<IDWriteRenderingParams> renderingParams;
    if (FAILED(factory->CreateCustomRenderingParams(
            2.2F, 1.0F, 0.0F, DWRITE_PIXEL_GEOMETRY_FLAT,
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            renderingParams.put()))) {
      return result;
    }
    auto* renderer = new BitmapTextRenderer(target.get(), renderingParams.get());
    const HRESULT drawStatus = layout->Draw(
        nullptr, renderer, static_cast<FLOAT>(padding),
        static_cast<FLOAT>(padding));
    renderer->Release();
    if (FAILED(drawStatus)) return result;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint8_t> bgra(
        static_cast<std::size_t>(width) * height * 4U);
    const HBITMAP bitmap = static_cast<HBITMAP>(GetCurrentObject(
        target->GetMemoryDC(), OBJ_BITMAP));
    if (!bitmap || GetDIBits(
            target->GetMemoryDC(), bitmap, 0, static_cast<UINT>(height),
            bgra.data(), &info, DIB_RGB_COLORS) == 0) {
      return result;
    }

    int cropX1 = width;
    int cropY1 = height;
    int cropX2 = 0;
    int cropY2 = 0;
    auto coverageAt = [&](int x, int y) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * width + x) * 4U;
      return std::max({bgra[offset], bgra[offset + 1], bgra[offset + 2]});
    };
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (coverageAt(x, y) == 0) continue;
        cropX1 = std::min(cropX1, x);
        cropY1 = std::min(cropY1, y);
        cropX2 = std::max(cropX2, x + 1);
        cropY2 = std::max(cropY2, y + 1);
      }
    }
    if (cropX1 >= cropX2 || cropY1 >= cropY2) return {};
    result.width = cropX2 - cropX1;
    result.height = cropY2 - cropY1;
    result.fillPixels.resize(
        static_cast<std::size_t>(result.width) * result.height);
    for (int y = 0; y < result.height; ++y) {
      const int sourceY = cropY2 - 1 - y;
      for (int x = 0; x < result.width; ++x) {
        result.fillPixels[static_cast<std::size_t>(y) * result.width + x] =
            coverageAt(cropX1 + x, sourceY);
      }
    }
    result.resolvedFont = fontFamily.empty() || fontFamily == "System Default"
        ? "Segoe UI" : fontFamily;
    return result;
  } catch (...) {
    return {};
  }
}

}  // namespace wipreview::text
