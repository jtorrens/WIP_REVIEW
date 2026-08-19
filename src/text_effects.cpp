#include "text_rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace wipreview::text {

bool addOutline(GlyphRaster& glyph, int radiusPixels) noexcept {
  try {
    if (radiusPixels <= 0 || glyph.width <= 0 || glyph.height <= 0 ||
        glyph.fillPixels.size() != static_cast<std::size_t>(glyph.width) *
                                   static_cast<std::size_t>(glyph.height)) {
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
    if (pixelCount > 128U * 1024U * 1024U) return false;

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

}  // namespace wipreview::text
