#include "probe_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace wipreview::probe {

RectI intersect(RectI a, RectI b) noexcept {
  return {std::max(a.x1, b.x1), std::max(a.y1, b.y1),
          std::min(a.x2, b.x2), std::min(a.y2, b.y2)};
}

bool empty(RectI rect) noexcept {
  return rect.x1 >= rect.x2 || rect.y1 >= rect.y2;
}

namespace {

std::byte* pixelAddress(const ImageView& image, int x, int y) noexcept {
  return image.data + static_cast<std::ptrdiff_t>(y - image.bounds.y1) * image.rowBytes
       + static_cast<std::ptrdiff_t>(x - image.bounds.x1)
             * static_cast<std::ptrdiff_t>(image.pixelBytes);
}

float halfToFloat(std::uint16_t value) noexcept {
  const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
  std::uint32_t exponent = (value >> 10U) & 0x1fU;
  std::uint32_t mantissa = value & 0x03ffU;
  std::uint32_t result = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      result = sign;
    } else {
      int shift = 0;
      while ((mantissa & 0x0400U) == 0) {
        mantissa <<= 1U;
        ++shift;
      }
      mantissa &= 0x03ffU;
      result = sign | static_cast<std::uint32_t>(127 - 15 - shift) << 23U
                    | mantissa << 13U;
    }
  } else if (exponent == 0x1fU) {
    result = sign | 0x7f800000U | mantissa << 13U;
  } else {
    result = sign | (exponent + 112U) << 23U | mantissa << 13U;
  }
  float converted = 0.0F;
  std::memcpy(&converted, &result, sizeof(converted));
  return converted;
}

std::uint16_t floatToHalf(float value) noexcept {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const std::uint32_t sign = (bits >> 16U) & 0x8000U;
  const std::uint32_t mantissa = bits & 0x007fffffU;
  const int exponent = static_cast<int>((bits >> 23U) & 0xffU) - 127 + 15;
  if (exponent <= 0) {
    if (exponent < -10) return static_cast<std::uint16_t>(sign);
    const std::uint32_t rounded = (mantissa | 0x00800000U)
                                >> static_cast<unsigned>(14 - exponent);
    return static_cast<std::uint16_t>(sign | rounded);
  }
  if (exponent >= 31) {
    return static_cast<std::uint16_t>(sign | 0x7c00U);
  }
  return static_cast<std::uint16_t>(sign | static_cast<std::uint32_t>(exponent) << 10U
                                    | (mantissa + 0x00001000U) >> 13U);
}

float readChannel(const std::byte* pixel, int channel, ChannelType type) noexcept {
  switch (type) {
    case ChannelType::UInt8:
      return static_cast<float>(reinterpret_cast<const std::uint8_t*>(pixel)[channel]) / 255.0F;
    case ChannelType::UInt16:
      return static_cast<float>(reinterpret_cast<const std::uint16_t*>(pixel)[channel]) / 65535.0F;
    case ChannelType::Half:
      return halfToFloat(reinterpret_cast<const std::uint16_t*>(pixel)[channel]);
    case ChannelType::Float32:
      return reinterpret_cast<const float*>(pixel)[channel];
  }
  return 0.0F;
}

void writeChannel(std::byte* pixel, int channel, ChannelType type, float value) noexcept {
  switch (type) {
    case ChannelType::UInt8:
      reinterpret_cast<std::uint8_t*>(pixel)[channel] = static_cast<std::uint8_t>(
          std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
      return;
    case ChannelType::UInt16:
      reinterpret_cast<std::uint16_t*>(pixel)[channel] = static_cast<std::uint16_t>(
          std::lround(std::clamp(value, 0.0F, 1.0F) * 65535.0F));
      return;
    case ChannelType::Half:
      reinterpret_cast<std::uint16_t*>(pixel)[channel] = floatToHalf(value);
      return;
    case ChannelType::Float32:
      reinterpret_cast<float*>(pixel)[channel] = value;
      return;
  }
}

void writePixel(const ImageView& image, int x, int y,
                const std::array<float, 4>& rgba) noexcept {
  std::byte* pixel = pixelAddress(image, x, y);
  for (int channel = 0; channel < image.channels; ++channel) {
    const int rgbaChannel = image.channels == 1 ? 3 : channel;
    writeChannel(pixel, channel, image.channelType,
                 rgba[static_cast<std::size_t>(rgbaChannel)]);
  }
}

double sinc(double value) noexcept {
  if (std::abs(value) < 1.0e-12) return 1.0;
  constexpr double pi = 3.14159265358979323846;
  const double angle = pi * value;
  return std::sin(angle) / angle;
}

double kernel(double distance, ResampleFilter filter) noexcept {
  const double x = std::abs(distance);
  if (filter == ResampleFilter::Bilinear) return x < 1.0 ? 1.0 - x : 0.0;
  if (filter == ResampleFilter::Bicubic) {
    // Catmull-Rom cubic (a=-0.5).
    if (x < 1.0) return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2.0) return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
  }
  return x < 3.0 ? sinc(x) * sinc(x / 3.0) : 0.0;
}

std::array<float, 4> sample(const ImageView& source, double x, double y,
                            ResampleFilter filter, bool sourcePremultiplied,
                            bool outputPremultiplied) noexcept {
  std::array<double, 4> sum{};
  double weightSum = 0.0;
  const int radius = filter == ResampleFilter::Bilinear ? 1
                   : filter == ResampleFilter::Bicubic ? 2 : 3;
  const int firstX = static_cast<int>(std::floor(x)) - radius + 1;
  const int firstY = static_cast<int>(std::floor(y)) - radius + 1;
  for (int iy = firstY; iy < firstY + radius * 2; ++iy) {
    const double wy = kernel(y - static_cast<double>(iy), filter);
    if (wy == 0.0) continue;
    const int sy = std::clamp(iy, source.bounds.y1, source.bounds.y2 - 1);
    for (int ix = firstX; ix < firstX + radius * 2; ++ix) {
      const double weight = wy * kernel(x - static_cast<double>(ix), filter);
      if (weight == 0.0) continue;
      const int sx = std::clamp(ix, source.bounds.x1, source.bounds.x2 - 1);
      const std::byte* pixel = pixelAddress(source, sx, sy);
      std::array<float, 4> rgba{0.0F, 0.0F, 0.0F, 1.0F};
      if (source.channels == 1) {
        rgba[3] = readChannel(pixel, 0, source.channelType);
      } else {
        for (int channel = 0; channel < source.channels; ++channel) {
          rgba[static_cast<std::size_t>(channel)] = readChannel(pixel, channel, source.channelType);
        }
      }
      if (!sourcePremultiplied) {
        for (std::size_t channel = 0; channel < 3; ++channel) rgba[channel] *= rgba[3];
      }
      for (std::size_t channel = 0; channel < 4; ++channel) {
        sum[channel] += static_cast<double>(rgba[channel]) * weight;
      }
      weightSum += weight;
    }
  }
  std::array<float, 4> result{};
  if (std::abs(weightSum) < std::numeric_limits<double>::epsilon()) return result;
  for (std::size_t channel = 0; channel < 4; ++channel) {
    result[channel] = static_cast<float>(sum[channel] / weightSum);
  }
  if (!outputPremultiplied && result[3] > 1.0e-8F) {
    for (std::size_t channel = 0; channel < 3; ++channel) result[channel] /= result[3];
  }
  return result;
}

}  // namespace

void copyProbeFrame(const ImageView& source,
                    const ImageView& destination,
                    RectI renderWindow) noexcept {
  if (!destination.data || destination.pixelBytes == 0) {
    return;
  }

  const RectI writable = intersect(renderWindow, destination.bounds);
  if (empty(writable)) {
    return;
  }

  const auto writableBytes = static_cast<std::size_t>(writable.x2 - writable.x1)
                           * destination.pixelBytes;
  for (int y = writable.y1; y < writable.y2; ++y) {
    std::memset(pixelAddress(destination, writable.x1, y), 0, writableBytes);
  }

  if (!source.data || source.pixelBytes != destination.pixelBytes) {
    return;
  }

  const RectI copyArea = intersect(writable, source.bounds);
  if (empty(copyArea)) {
    return;
  }

  const auto copyBytes = static_cast<std::size_t>(copyArea.x2 - copyArea.x1)
                       * destination.pixelBytes;
  for (int y = copyArea.y1; y < copyArea.y2; ++y) {
    std::memcpy(pixelAddress(destination, copyArea.x1, y),
                pixelAddress(source, copyArea.x1, y), copyBytes);
  }
}

PlacementTransform computePlacement(RectI sourceBounds, RectI outputBounds,
                                    const RenderOptions& options) noexcept {
  const double sourceWidth = std::max(1, sourceBounds.x2 - sourceBounds.x1);
  const double sourceHeight = std::max(1, sourceBounds.y2 - sourceBounds.y1);
  const double outputWidth = std::max(1, outputBounds.x2 - outputBounds.x1);
  const double outputHeight = std::max(1, outputBounds.y2 - outputBounds.y1);
  PlacementTransform transform;
  transform.sourceCenterX = (sourceBounds.x1 + sourceBounds.x2) * 0.5;
  transform.sourceCenterY = (sourceBounds.y1 + sourceBounds.y2) * 0.5;
  transform.outputCenterX = (outputBounds.x1 + outputBounds.x2) * 0.5;
  transform.outputCenterY = (outputBounds.y1 + outputBounds.y2) * 0.5;

  if (options.placement == PlacementMode::Identity ||
      options.placement == PlacementMode::OneToOne) {
    return transform;
  }
  if (options.placement == PlacementMode::Stretch) {
    transform.scaleX = outputWidth / sourceWidth;
    transform.scaleY = outputHeight / sourceHeight;
    return transform;
  }
  const double sourcePAR = options.sourcePixelAspect > 0.0 ? options.sourcePixelAspect : 1.0;
  const double outputPAR = options.outputPixelAspect > 0.0 ? options.outputPixelAspect : 1.0;
  const double horizontalScale = outputWidth * outputPAR / (sourceWidth * sourcePAR);
  const double verticalScale = outputHeight / sourceHeight;
  const double displayScale = options.placement == PlacementMode::Fit
      ? std::min(horizontalScale, verticalScale)
      : std::max(horizontalScale, verticalScale);
  transform.scaleX = displayScale * sourcePAR / outputPAR;
  transform.scaleY = displayScale;
  return transform;
}

void renderStaticFrame(const ImageView& source, const ImageView& destination,
                       RectI renderWindow, const RenderOptions& options) noexcept {
  if (!destination.data || destination.pixelBytes == 0 || destination.channels <= 0) return;
  const RectI writable = intersect(renderWindow, destination.bounds);
  if (empty(writable)) return;
  std::array<float, 4> canvas{options.canvas[0], options.canvas[1],
                              options.canvas[2], options.canvas[3]};
  if (options.outputPremultiplied) {
    for (std::size_t channel = 0; channel < 3; ++channel) canvas[channel] *= canvas[3];
  }
  if (!source.data || source.pixelBytes == 0 || source.channels <= 0 || empty(source.bounds)) {
    for (int y = writable.y1; y < writable.y2; ++y)
      for (int x = writable.x1; x < writable.x2; ++x) writePixel(destination, x, y, canvas);
    return;
  }

  const PlacementTransform transform = computePlacement(source.bounds, destination.bounds, options);
  for (int y = writable.y1; y < writable.y2; ++y) {
    for (int x = writable.x1; x < writable.x2; ++x) {
      double sourceX = static_cast<double>(x) + 0.5;
      double sourceY = static_cast<double>(y) + 0.5;
      if (options.placement != PlacementMode::Identity) {
        sourceX = transform.sourceCenterX
                + (static_cast<double>(x) + 0.5 - transform.outputCenterX) / transform.scaleX;
        sourceY = transform.sourceCenterY
                + (static_cast<double>(y) + 0.5 - transform.outputCenterY) / transform.scaleY;
      }
      if (sourceX < source.bounds.x1 || sourceX >= source.bounds.x2 ||
          sourceY < source.bounds.y1 || sourceY >= source.bounds.y2) {
        writePixel(destination, x, y, canvas);
        continue;
      }
      // Image samples live at integer pixel centres after subtracting 0.5 from
      // the canonical coordinate used by the placement transform.
      writePixel(destination, x, y,
                 sample(source, sourceX - 0.5, sourceY - 0.5, options.filter,
                        options.sourcePremultiplied, options.outputPremultiplied));
    }
  }
}

}  // namespace wipreview::probe
