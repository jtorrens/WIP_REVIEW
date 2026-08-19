#pragma once

#include <array>

namespace wipreview::color {

enum class DisplayEncoding {
  Rec709Gamma24,
  Rec2100PQ,
  Rec2100HLG,
};

struct DisplayConfig {
  DisplayEncoding encoding = DisplayEncoding::Rec709Gamma24;
  double graphicsWhiteNits = 100.0;
  double peakNits = 1000.0;
};

using RGB = std::array<float, 3>;

// Returns the V1 automatic graphics-white value for the selected output.
// HLG derives reference white from the configured peak display luminance.
[[nodiscard]] double automaticGraphicsWhiteNits(
    DisplayEncoding encoding, double peakNits = 1000.0) noexcept;

// Display-light linear is normalized so 1.0 equals Graphics White. PQ and HLG
// are decoded through absolute luminance before normalization. Rec.709 uses
// reference white as its unit luminance.
[[nodiscard]] RGB decodeDisplay(const RGB& encoded,
                                const DisplayConfig& config) noexcept;
[[nodiscard]] RGB encodeDisplay(const RGB& displayLinear,
                                const DisplayConfig& config) noexcept;

// UI graphics colours are relative to Graphics White, so the picker value can
// be composed directly in the normalized display-light representation.
[[nodiscard]] RGB graphicsColourToDisplayLinear(const RGB& picker) noexcept;

}  // namespace wipreview::color
