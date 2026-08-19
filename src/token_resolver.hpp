#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace wipreview::tokens {

enum class DropFrameMode {
  Auto,
  NonDrop,
  Drop,
};

struct Settings {
  std::int64_t frameRelativeBase = 1;
  std::int64_t frameStart = 1001;
  double fps = 24.0;
  std::string timecodeStart = "00:00:00:00";
  DropFrameMode dropFrameMode = DropFrameMode::Auto;
};

struct Resolution {
  std::string text;
  std::string timecode;
  std::int64_t effectFrame = 0;
  std::int64_t frameRelative = 1;
  std::int64_t frame = 1001;
  int nominalFps = 24;
  bool containsDynamicTokens = false;
  bool fpsValid = true;
  bool dropCompatible = false;
  bool dropApplied = false;
  bool timecodeStartValid = true;
  bool usedTimecodeFallback = false;
};

[[nodiscard]] bool containsDynamicToken(std::string_view text) noexcept;

// Resolves only the three V1 tokens. Unknown brace expressions remain literal.
// Invalid timecode state uses frameRelative as the internal frame offset and is
// surfaced in the result instead of silently approximating the requested start.
[[nodiscard]] Resolution resolve(std::string_view text,
                                 double effectTime,
                                 const Settings& settings) noexcept;

}  // namespace wipreview::tokens
