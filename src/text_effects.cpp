#include "text_rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace wipreview::text {
namespace {

constexpr std::size_t kMaximumLayerPixels = 32U * 1024U * 1024U;

bool validFill(const GlyphRaster& glyph) noexcept {
  return glyph.width > 0 && glyph.height > 0 &&
      glyph.fillPixels.size() == static_cast<std::size_t>(glyph.width) *
                                 static_cast<std::size_t>(glyph.height);
}

std::vector<std::uint8_t> padLayer(const std::vector<std::uint8_t>& source,
                                   int sourceWidth,
                                   int sourceHeight,
                                   int outputWidth,
                                   int outputHeight,
                                   int offsetX,
                                   int offsetY) {
  if (source.empty()) return {};
  std::vector<std::uint8_t> result(
      static_cast<std::size_t>(outputWidth) * static_cast<std::size_t>(outputHeight), 0);
  for (int y = 0; y < sourceHeight; ++y) {
    std::copy_n(source.data() + static_cast<std::size_t>(y * sourceWidth),
                sourceWidth,
                result.data() + static_cast<std::size_t>(
                    (y + offsetY) * outputWidth + offsetX));
  }
  return result;
}

void gaussianBlur(std::vector<std::uint8_t>& mask, int width, int height,
                  double sigma) {
  if (sigma <= 0.0 || mask.empty()) return;
  const int radius = static_cast<int>(std::ceil(3.0 * sigma));
  std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
  double kernelSum = 0.0;
  for (int offset = -radius; offset <= radius; ++offset) {
    const double value = std::exp(-0.5 * static_cast<double>(offset * offset) /
                                  (sigma * sigma));
    kernel[static_cast<std::size_t>(offset + radius)] = value;
    kernelSum += value;
  }
  for (auto& value : kernel) value /= kernelSum;

  const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height);
  std::vector<float> horizontal(pixelCount, 0.0F);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double sum = 0.0;
      for (int offset = -radius; offset <= radius; ++offset) {
        const int sourceX = x + offset;
        if (sourceX < 0 || sourceX >= width) continue;
        sum += static_cast<double>(mask[static_cast<std::size_t>(y * width + sourceX)]) *
               kernel[static_cast<std::size_t>(offset + radius)];
      }
      horizontal[static_cast<std::size_t>(y * width + x)] = static_cast<float>(sum);
    }
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double sum = 0.0;
      for (int offset = -radius; offset <= radius; ++offset) {
        const int sourceY = y + offset;
        if (sourceY < 0 || sourceY >= height) continue;
        sum += static_cast<double>(horizontal[
                   static_cast<std::size_t>(sourceY * width + x)]) *
               kernel[static_cast<std::size_t>(offset + radius)];
      }
      mask[static_cast<std::size_t>(y * width + x)] =
          static_cast<std::uint8_t>(std::clamp(std::lround(sum), 0L, 255L));
    }
  }
}

}  // namespace

TextLayoutResult layoutUTF8(const TextLayoutRequest& request) noexcept {
  TextLayoutResult result;
  try {
    const double requestedSize =
        std::clamp(request.requestedPixelSize, 1.0, 4096.0);
    result.renderedText = request.text;
    result.effectivePixelSize = requestedSize;
    result.glyph = rasterizeUTF8(
        request.text, request.fontFamily, request.fontStyle, requestedSize);
    return result;
  } catch (...) {
    return {};
  }
}

bool addOutline(GlyphRaster& glyph, int radiusPixels) noexcept {
  try {
    if (radiusPixels <= 0 || !validFill(glyph)) {
      return false;
    }

    const int radius = std::min(radiusPixels, 4096);
    if (glyph.width > std::numeric_limits<int>::max() - 2 * radius ||
        glyph.height > std::numeric_limits<int>::max() - 2 * radius) {
      return false;
    }
    const int sourceWidth = glyph.width;
    const int sourceHeight = glyph.height;
    const int outputWidth = sourceWidth + 2 * radius;
    const int outputHeight = sourceHeight + 2 * radius;
    const std::size_t pixelCount = static_cast<std::size_t>(outputWidth) *
                                   static_cast<std::size_t>(outputHeight);
    if (pixelCount > kMaximumLayerPixels) return false;

    std::vector<std::uint8_t> paddedFill(pixelCount, 0);
    std::vector<std::uint8_t> outline(pixelCount, 0);
    for (int y = 0; y < sourceHeight; ++y) {
      const auto* sourceRow = glyph.fillPixels.data() +
          static_cast<std::size_t>(y * sourceWidth);
      auto* fillRow = paddedFill.data() +
          static_cast<std::size_t>((y + radius) * outputWidth + radius);
      std::copy_n(sourceRow, sourceWidth, fillRow);

      for (int deltaY = -radius; deltaY <= radius; ++deltaY) {
        const auto squaredX = radius * radius - deltaY * deltaY;
        const int halfWidth = static_cast<int>(std::floor(std::sqrt(
            static_cast<double>(std::max(0, squaredX)))));
        const int outputY = y + radius + deltaY;
        auto* outputRow = outline.data() +
            static_cast<std::size_t>(outputY * outputWidth);
        std::deque<int> maxima;
        int nextSourceX = 0;
        for (int x = -halfWidth; x < sourceWidth + halfWidth; ++x) {
          const int right = std::min(sourceWidth - 1, x + halfWidth);
          while (nextSourceX <= right) {
            while (!maxima.empty() &&
                   sourceRow[maxima.back()] <= sourceRow[nextSourceX]) {
              maxima.pop_back();
            }
            maxima.push_back(nextSourceX++);
          }
          const int left = std::max(0, x - halfWidth);
          while (!maxima.empty() && maxima.front() < left) maxima.pop_front();
          if (!maxima.empty()) {
            auto& destination = outputRow[x + radius];
            destination = std::max(destination, sourceRow[maxima.front()]);
          }
        }
      }
    }

    glyph.fillPixels = std::move(paddedFill);
    glyph.outlinePixels = std::move(outline);
    glyph.width = outputWidth;
    glyph.height = outputHeight;
    return true;
  } catch (...) {
    return false;
  }
}

bool addShadow(GlyphRaster& glyph, int offsetXPixels, int offsetDownPixels,
               double softnessPixels) noexcept {
  try {
    if (!validFill(glyph) || !std::isfinite(softnessPixels) ||
        softnessPixels < 0.0) {
      return false;
    }
    const int blurRadius = static_cast<int>(std::ceil(
        3.0 * std::min(softnessPixels, 4096.0)));
    const int offsetY = -offsetDownPixels;
    const long long minimumX = std::min<long long>(0, offsetXPixels - blurRadius);
    const long long minimumY = std::min<long long>(0, offsetY - blurRadius);
    const long long maximumX = std::max<long long>(
        glyph.width, static_cast<long long>(offsetXPixels) + glyph.width + blurRadius);
    const long long maximumY = std::max<long long>(
        glyph.height, static_cast<long long>(offsetY) + glyph.height + blurRadius);
    const long long outputWidth64 = maximumX - minimumX;
    const long long outputHeight64 = maximumY - minimumY;
    if (outputWidth64 <= 0 || outputHeight64 <= 0 ||
        outputWidth64 > std::numeric_limits<int>::max() ||
        outputHeight64 > std::numeric_limits<int>::max()) {
      return false;
    }
    const int outputWidth = static_cast<int>(outputWidth64);
    const int outputHeight = static_cast<int>(outputHeight64);
    const std::size_t pixelCount = static_cast<std::size_t>(outputWidth) *
                                   static_cast<std::size_t>(outputHeight);
    if (pixelCount > kMaximumLayerPixels) return false;

    const int layerOffsetX = static_cast<int>(-minimumX);
    const int layerOffsetY = static_cast<int>(-minimumY);
    auto fill = padLayer(glyph.fillPixels, glyph.width, glyph.height,
                         outputWidth, outputHeight, layerOffsetX, layerOffsetY);
    auto outline = padLayer(glyph.outlinePixels, glyph.width, glyph.height,
                            outputWidth, outputHeight, layerOffsetX, layerOffsetY);
    std::vector<std::uint8_t> shadow(pixelCount, 0);
    const int shadowX = layerOffsetX + offsetXPixels;
    const int shadowY = layerOffsetY + offsetY;
    for (int y = 0; y < glyph.height; ++y) {
      std::copy_n(glyph.fillPixels.data() + static_cast<std::size_t>(y * glyph.width),
                  glyph.width,
                  shadow.data() + static_cast<std::size_t>(
                      (y + shadowY) * outputWidth + shadowX));
    }
    gaussianBlur(shadow, outputWidth, outputHeight,
                 std::min(softnessPixels, 4096.0));

    glyph.fillPixels = std::move(fill);
    glyph.outlinePixels = std::move(outline);
    glyph.shadowPixels = std::move(shadow);
    glyph.width = outputWidth;
    glyph.height = outputHeight;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace wipreview::text
