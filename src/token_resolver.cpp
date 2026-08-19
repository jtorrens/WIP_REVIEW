#include "token_resolver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace wipreview::tokens {
namespace {

constexpr double kFps29_97 = 30000.0 / 1001.0;
constexpr double kFps59_94 = 60000.0 / 1001.0;

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

bool compatibleDropRate(double fps) noexcept {
  return std::abs(fps - kFps29_97) < 0.01 ||
         std::abs(fps - kFps59_94) < 0.01;
}

int nominalRate(double fps) noexcept {
  return std::clamp(static_cast<int>(std::lround(fps)), 1, 240);
}

std::int64_t positiveModulo(std::int64_t value, std::int64_t modulus) noexcept {
  if (modulus <= 0) return 0;
  const std::int64_t result = value % modulus;
  return result < 0 ? result + modulus : result;
}

bool parseTimecode(const std::string& value, int nominalFps, bool drop,
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
  if (first != ':' || second != ':' || (third != ':' && third != ';') ||
      hours < 0 || hours > 23 || minutes < 0 || minutes > 59 ||
      seconds < 0 || seconds > 59 || frame < 0 || frame >= nominalFps) {
    return false;
  }
  const int dropFrames = drop ? nominalFps / 15 : 0;
  if (drop && minutes % 10 != 0 && seconds == 0 && frame < dropFrames) {
    return false;
  }
  const std::int64_t totalMinutes = static_cast<std::int64_t>(hours) * 60 + minutes;
  frames = (static_cast<std::int64_t>(hours) * 3600 + minutes * 60 + seconds) *
      nominalFps + frame;
  if (drop) frames -= static_cast<std::int64_t>(dropFrames) *
      (totalMinutes - totalMinutes / 10);
  return true;
}

std::string formatTimecode(std::int64_t actualFrames, int nominalFps,
                           bool drop) {
  std::int64_t labelledFrames = actualFrames;
  std::int64_t framesPer24Hours = static_cast<std::int64_t>(nominalFps) * 86400;
  if (drop) {
    const int dropFrames = nominalFps / 15;
    const std::int64_t framesPerHour =
        static_cast<std::int64_t>(nominalFps) * 3600 - dropFrames * 54;
    framesPer24Hours = framesPerHour * 24;
    const std::int64_t framesPer10Minutes =
        static_cast<std::int64_t>(nominalFps) * 600 - dropFrames * 9;
    const std::int64_t framesPerMinute =
        static_cast<std::int64_t>(nominalFps) * 60 - dropFrames;
    labelledFrames = positiveModulo(actualFrames, framesPer24Hours);
    const std::int64_t tenMinuteBlocks = labelledFrames / framesPer10Minutes;
    const std::int64_t remainder = labelledFrames % framesPer10Minutes;
    labelledFrames += static_cast<std::int64_t>(dropFrames) * 9 * tenMinuteBlocks;
    if (remainder >= dropFrames) {
      labelledFrames += static_cast<std::int64_t>(dropFrames) *
          ((remainder - dropFrames) / framesPerMinute);
    }
  } else {
    labelledFrames = positiveModulo(actualFrames, framesPer24Hours);
  }

  const int frame = static_cast<int>(labelledFrames % nominalFps);
  const std::int64_t totalSeconds = labelledFrames / nominalFps;
  const int seconds = static_cast<int>(totalSeconds % 60);
  const int minutes = static_cast<int>((totalSeconds / 60) % 60);
  const int hours = static_cast<int>((totalSeconds / 3600) % 24);
  std::array<char, 16> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%02d:%02d:%02d%c%02d",
                hours, minutes, seconds, drop ? ';' : ':', frame);
  return buffer.data();
}

void replaceAll(std::string& text, std::string_view token,
                const std::string& value) {
  std::size_t offset = 0;
  while ((offset = text.find(token, offset)) != std::string::npos) {
    text.replace(offset, token.size(), value);
    offset += value.size();
  }
}

}  // namespace

bool containsDynamicToken(std::string_view text) noexcept {
  return text.find("{frame_rel}") != std::string_view::npos ||
         text.find("{frame}") != std::string_view::npos ||
         text.find("{timecode}") != std::string_view::npos;
}

Resolution resolve(std::string_view text, double effectTime,
                   const Settings& settings) noexcept {
  Resolution result;
  try {
    result.text = std::string(text);
    result.containsDynamicTokens = containsDynamicToken(text);
    result.effectFrame = roundedFrame(effectTime);
    result.frameRelative = saturatingAdd(result.effectFrame, settings.frameRelativeBase);
    result.frame = saturatingAdd(result.effectFrame, settings.frameStart);

    result.fpsValid = std::isfinite(settings.fps) &&
        settings.fps >= 1.0 && settings.fps <= 240.0;
    const double effectiveFps = result.fpsValid ? settings.fps : 24.0;
    result.nominalFps = nominalRate(effectiveFps);
    result.dropCompatible = compatibleDropRate(effectiveFps);
    result.dropApplied = settings.dropFrameMode == DropFrameMode::Auto
        ? result.dropCompatible
        : settings.dropFrameMode == DropFrameMode::Drop && result.dropCompatible;

    std::int64_t startFrames = 0;
    result.timecodeStartValid = parseTimecode(
        settings.timecodeStart, result.nominalFps, result.dropApplied, startFrames);
    result.usedTimecodeFallback = !result.fpsValid || !result.timecodeStartValid ||
        (settings.dropFrameMode == DropFrameMode::Drop && !result.dropCompatible);
    const std::int64_t timecodeFrames = result.usedTimecodeFallback
        ? result.frameRelative
        : saturatingAdd(startFrames, result.effectFrame);
    result.timecode = formatTimecode(
        timecodeFrames, result.nominalFps, result.dropApplied);

    replaceAll(result.text, "{frame_rel}", std::to_string(result.frameRelative));
    replaceAll(result.text, "{frame}", std::to_string(result.frame));
    replaceAll(result.text, "{timecode}", result.timecode);
    return result;
  } catch (...) {
    return {};
  }
}

}  // namespace wipreview::tokens
