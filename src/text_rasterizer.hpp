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

struct GlyphMask {
  std::vector<std::uint8_t> pixels;
  int width = 0;
  int height = 0;
  std::string resolvedFont;
  bool usedFallback = false;

  [[nodiscard]] probe::GlyphMaskView view() const noexcept {
    return {pixels.empty() ? nullptr : pixels.data(), width, height, width};
  }
};

[[nodiscard]] GlyphMask rasterizeUTF8(const std::string& text,
                                      const std::string& fontFamily,
                                      FontStyle style,
                                      double pixelSize) noexcept;

}  // namespace wipreview::text
