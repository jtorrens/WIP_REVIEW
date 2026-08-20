#pragma once

#include <cstdint>
#include <string>

namespace wipreview::fields {

enum class CalculatedField {
  None,
  FrameRelative,
  Frame,
  Timecode,
  Date,
  SourceFrame,
  SourceFilename,
};

struct Settings {
  std::int64_t frameRelativeBase = 1;
  std::int64_t frameStart = 1001;
  double fps = 24.0;
  std::string timecodeStart = "00:00:00:00";
  std::string reviewDate;
  std::string sourceFrame;
  std::string sourceFilename;
};

struct Resolution {
  std::string text;
  std::string timecode;
  std::int64_t effectFrame = 0;
  std::int64_t frameRelative = 1;
  std::int64_t frame = 1001;
  int nominalFps = 24;
  bool containsCalculatedField = false;
  bool fpsValid = true;
  bool timecodeStartValid = true;
  bool usedTimecodeFallback = false;
};

// Appends the selected calculated value to the literal prefix. Invalid
// timecode state uses frameRelative as the internal frame offset and is
// surfaced in the result instead of silently approximating the requested start.
[[nodiscard]] Resolution resolve(const std::string& prefix,
                                 CalculatedField field,
                                 double effectTime,
                                 const Settings& settings) noexcept;

}  // namespace wipreview::fields
