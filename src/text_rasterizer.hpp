#pragma once

#include "probe_core.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wipreview::text {

enum class FontStyle {
  Regular,
  Bold,
  Italic,
  BoldItalic,
};

struct GlyphRaster {
  std::vector<std::uint8_t> fillPixels;
  std::vector<std::uint8_t> outlinePixels;
  int width = 0;
  int height = 0;
  std::string resolvedFont;
  bool usedFallback = false;

  [[nodiscard]] probe::GlyphMaskView fillView() const noexcept {
    return {fillPixels.empty() ? nullptr : fillPixels.data(), width, height, width};
  }

  [[nodiscard]] probe::GlyphMaskView outlineView() const noexcept {
    return {outlinePixels.empty() ? nullptr : outlinePixels.data(), width, height, width};
  }
};

[[nodiscard]] GlyphRaster rasterizeUTF8(const std::string& text,
                                        const std::string& fontFamily,
                                        FontStyle style,
                                        double pixelSize) noexcept;

// Expands the raster canvas and dilates the real glyph alpha with a circular
// structuring element. The padded fill and outline masks retain identical
// dimensions so a single anchor positions both layers exactly.
[[nodiscard]] bool addOutline(GlyphRaster& glyph, int radiusPixels) noexcept;

}  // namespace wipreview::text
