#include "calculated_field_resolver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace wipreview::fields {
namespace {

std::int64_t saturatingAdd(std::int64_t left, std::int64_t right) noexcept {
  if (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) {
    return std::numeric_limits<std::int64_t>::max();
  }
  if (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return left + right;
}

std::int64_t roundedFrame(double time) noexcept {
  if (!std::isfinite(time)) return 0;
  const double minimum = static_cast<double>(std::numeric_limits<std::int64_t>::min());
  const double maximum = static_cast<double>(std::numeric_limits<std::int64_t>::max());
  if (time <= minimum) return std::numeric_limits<std::int64_t>::min();
  if (time >= maximum) return std::numeric_limits<std::int64_t>::max();
  return static_cast<std::int64_t>(std::llround(time));
}

int nominalRate(double fps) noexcept {
  return std::clamp(static_cast<int>(std::lround(fps)), 1, 240);
}

std::int64_t positiveModulo(std::int64_t value, std::int64_t modulus) noexcept {
  if (modulus <= 0) return 0;
  const std::int64_t result = value % modulus;
  return result < 0 ? result + modulus : result;
}

bool parseTimecode(const std::string& value, int nominalFps,
                   std::int64_t& frames) noexcept {
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int frame = 0;
  char first = 0;
  char second = 0;
  char third = 0;
  char trailing = 0;
  if (std::sscanf(value.c_str(), "%d%c%d%c%d%c%d%c",
                  &hours, &first, &minutes, &second, &seconds, &third,
                  &frame, &trailing) != 7) {
    return false;
  }
  if (first != ':' || second != ':' || third != ':' ||
      hours < 0 || hours > 23 || minutes < 0 || minutes > 59 ||
      seconds < 0 || seconds > 59 || frame < 0 || frame >= nominalFps) {
    return false;
  }
  frames = (static_cast<std::int64_t>(hours) * 3600 + minutes * 60 + seconds) *
      nominalFps + frame;
  return true;
}

std::string formatTimecode(std::int64_t actualFrames, int nominalFps) {
  const std::int64_t framesPer24Hours =
      static_cast<std::int64_t>(nominalFps) * 86400;
  const std::int64_t labelledFrames =
      positiveModulo(actualFrames, framesPer24Hours);

  const int frame = static_cast<int>(labelledFrames % nominalFps);
  const std::int64_t totalSeconds = labelledFrames / nominalFps;
  const int seconds = static_cast<int>(totalSeconds % 60);
  const int minutes = static_cast<int>((totalSeconds / 60) % 60);
  const int hours = static_cast<int>((totalSeconds / 3600) % 24);
  std::array<char, 16> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%02d:%02d:%02d:%02d",
                hours, minutes, seconds, frame);
  return buffer.data();
}

}  // namespace

Resolution resolve(const std::string& prefix, CalculatedField field,
                   double effectTime,
                   const Settings& settings) noexcept {
  Resolution result;
  try {
    result.text = prefix;
    result.containsCalculatedField = field != CalculatedField::None;
    result.effectFrame = roundedFrame(effectTime);
    result.frameRelative = saturatingAdd(result.effectFrame, settings.frameRelativeBase);
    result.frame = saturatingAdd(result.effectFrame, settings.frameStart);

    result.fpsValid = std::isfinite(settings.fps) &&
        settings.fps >= 1.0 && settings.fps <= 240.0;
    const double effectiveFps = result.fpsValid ? settings.fps : 24.0;
    result.nominalFps = nominalRate(effectiveFps);
    std::int64_t startFrames = 0;
    result.timecodeStartValid = parseTimecode(
        settings.timecodeStart, result.nominalFps, startFrames);
    result.usedTimecodeFallback = !result.fpsValid || !result.timecodeStartValid;
    const std::int64_t timecodeFrames = result.usedTimecodeFallback
        ? result.frameRelative
        : saturatingAdd(startFrames, result.effectFrame);
    result.timecode = formatTimecode(timecodeFrames, result.nominalFps);

    switch (field) {
      case CalculatedField::None:
        break;
      case CalculatedField::FrameRelative:
        result.text += std::to_string(result.frameRelative);
        break;
      case CalculatedField::Frame:
        result.text += std::to_string(result.frame);
        break;
      case CalculatedField::Timecode:
        result.text += result.timecode;
        break;
      case CalculatedField::Date:
        result.text += settings.reviewDate;
        break;
      case CalculatedField::SourceFrame:
        result.text += settings.sourceFrame;
        break;
      case CalculatedField::SourceFilename:
        result.text += settings.sourceFilename;
        break;
    }
    return result;
  } catch (...) {
    return {};
  }
}

}  // namespace wipreview::fields
