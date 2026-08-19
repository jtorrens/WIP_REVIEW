#include "color_pipeline.hpp"

#include <algorithm>
#include <cmath>

namespace wipreview::color {
namespace {

constexpr double kPqM1 = 2610.0 / 16384.0;
constexpr double kPqM2 = 2523.0 / 32.0;
constexpr double kPqC1 = 3424.0 / 4096.0;
constexpr double kPqC2 = 2413.0 / 128.0;
constexpr double kPqC3 = 2392.0 / 128.0;

constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 0.28466892;
constexpr double kHlgC = 0.55991073;
constexpr std::array<double, 3> kRec2020Luma{0.2627, 0.6780, 0.0593};

double validGraphicsWhite(const DisplayConfig& config) noexcept {
  if (std::isfinite(config.graphicsWhiteNits) && config.graphicsWhiteNits > 0.0) {
    return config.graphicsWhiteNits;
  }
  return automaticGraphicsWhiteNits(config.encoding, config.peakNits);
}

double validPeak(double peakNits) noexcept {
  return std::isfinite(peakNits) && peakNits > 0.0 ? peakNits : 1000.0;
}

double pqToNits(double encoded) noexcept {
  const double value = std::pow(std::clamp(encoded, 0.0, 1.0), 1.0 / kPqM2);
  const double numerator = std::max(value - kPqC1, 0.0);
  const double denominator = kPqC2 - kPqC3 * value;
  if (denominator <= 0.0) return 10000.0;
  return 10000.0 * std::pow(numerator / denominator, 1.0 / kPqM1);
}

double nitsToPq(double nits) noexcept {
  const double luminance = std::pow(std::clamp(nits / 10000.0, 0.0, 1.0), kPqM1);
  return std::pow((kPqC1 + kPqC2 * luminance) /
                      (1.0 + kPqC3 * luminance),
                  kPqM2);
}

double hlgInverseOetf(double encoded) noexcept {
  const double value = std::max(encoded, 0.0);
  if (value <= 0.5) return value * value / 3.0;
  return (std::exp((value - kHlgC) / kHlgA) + kHlgB) / 12.0;
}

double hlgOetf(double sceneLinear) noexcept {
  const double value = std::max(sceneLinear, 0.0);
  if (value <= 1.0 / 12.0) return std::sqrt(3.0 * value);
  return kHlgA * std::log(12.0 * value - kHlgB) + kHlgC;
}

double hlgSystemGamma(double peakNits) noexcept {
  return 1.2 + 0.42 * std::log10(validPeak(peakNits) / 1000.0);
}

double luminance(const std::array<double, 3>& rgb) noexcept {
  return kRec2020Luma[0] * rgb[0] + kRec2020Luma[1] * rgb[1] +
         kRec2020Luma[2] * rgb[2];
}

}  // namespace

double automaticGraphicsWhiteNits(DisplayEncoding encoding,
                                  double peakNits) noexcept {
  switch (encoding) {
    case DisplayEncoding::Rec709Gamma24:
      return 100.0;
    case DisplayEncoding::Rec2100PQ:
      return 203.0;
    case DisplayEncoding::Rec2100HLG:
      return validPeak(peakNits) * 0.203;
  }
  return 100.0;
}

RGB decodeDisplay(const RGB& encoded, const DisplayConfig& config) noexcept {
  RGB result{};
  const double white = validGraphicsWhite(config);
  if (config.encoding == DisplayEncoding::Rec709Gamma24) {
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
      const double value = encoded[channel];
      result[channel] = static_cast<float>(
          std::copysign(std::pow(std::abs(value), 2.4), value));
    }
    return result;
  }
  if (config.encoding == DisplayEncoding::Rec2100PQ) {
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
      result[channel] = static_cast<float>(pqToNits(encoded[channel]) / white);
    }
    return result;
  }

  std::array<double, 3> scene{};
  for (std::size_t channel = 0; channel < scene.size(); ++channel) {
    scene[channel] = hlgInverseOetf(encoded[channel]);
  }
  const double sceneLuminance = std::max(luminance(scene), 0.0);
  const double gamma = hlgSystemGamma(config.peakNits);
  const double scale = validPeak(config.peakNits) *
      (sceneLuminance > 0.0 ? std::pow(sceneLuminance, gamma - 1.0) : 0.0);
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    result[channel] = static_cast<float>(scene[channel] * scale / white);
  }
  return result;
}

RGB encodeDisplay(const RGB& displayLinear,
                  const DisplayConfig& config) noexcept {
  RGB result{};
  const double white = validGraphicsWhite(config);
  if (config.encoding == DisplayEncoding::Rec709Gamma24) {
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
      const double value = displayLinear[channel];
      result[channel] = static_cast<float>(
          std::copysign(std::pow(std::abs(value), 1.0 / 2.4), value));
    }
    return result;
  }
  if (config.encoding == DisplayEncoding::Rec2100PQ) {
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
      result[channel] = static_cast<float>(nitsToPq(displayLinear[channel] * white));
    }
    return result;
  }

  std::array<double, 3> displayNits{};
  for (std::size_t channel = 0; channel < displayNits.size(); ++channel) {
    displayNits[channel] = std::max(
        static_cast<double>(displayLinear[channel]) * white, 0.0);
  }
  const double peak = validPeak(config.peakNits);
  const double displayLuminance = std::max(luminance(displayNits), 0.0);
  const double gamma = hlgSystemGamma(peak);
  const double sceneLuminance = displayLuminance > 0.0
      ? std::pow(displayLuminance / peak, 1.0 / gamma) : 0.0;
  const double scale = sceneLuminance > 0.0
      ? peak * std::pow(sceneLuminance, gamma - 1.0) : 1.0;
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    result[channel] = static_cast<float>(hlgOetf(displayNits[channel] / scale));
  }
  return result;
}

RGB graphicsColourToDisplayLinear(const RGB& picker) noexcept {
  return picker;
}

}  // namespace wipreview::color
