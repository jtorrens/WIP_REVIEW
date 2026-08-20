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
  std::vector<std::uint8_t> shadowPixels;
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

  [[nodiscard]] probe::GlyphMaskView shadowView() const noexcept {
    return {shadowPixels.empty() ? nullptr : shadowPixels.data(), width, height, width};
  }
};

struct TextLayoutRequest {
  std::string text;
  std::string fontFamily;
  FontStyle fontStyle = FontStyle::Regular;
  double requestedPixelSize = 1.0;
};

struct TextLayoutResult {
  GlyphRaster glyph;
  std::string renderedText;
  double effectivePixelSize = 0.0;
};

[[nodiscard]] GlyphRaster rasterizeUTF8(const std::string& text,
                                        const std::string& fontFamily,
                                        FontStyle style,
                                        double pixelSize) noexcept;

// Rasterizes the complete string at the requested size. Positioning is
// controlled only by the selected frame anchor; text is never constrained,
// shortened, or scaled automatically.
[[nodiscard]] TextLayoutResult layoutUTF8(const TextLayoutRequest& request) noexcept;

// Expands the raster canvas and dilates the real glyph alpha with a circular
// structuring element. The padded fill and outline masks retain identical
// dimensions so a single anchor positions both layers exactly.
[[nodiscard]] bool addOutline(GlyphRaster& glyph, int radiusPixels) noexcept;

// Builds the shadow from glyph fill alpha, then expands every existing layer
// into one shared canvas. Positive X moves right and positive Y moves visually
// down, matching the public shadow controls rather than bottom-up OFX Y.
[[nodiscard]] bool addShadow(GlyphRaster& glyph,
                             int offsetXPixels,
                             int offsetDownPixels,
                             double softnessPixels) noexcept;

}  // namespace wipreview::text
