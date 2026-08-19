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

std::array<float, 4> readRGBA(const ImageView& source, int x, int y,
                              bool sourcePremultiplied,
                              bool outputPremultiplied) noexcept {
  const std::byte* pixel = pixelAddress(source, x, y);
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
  if (!outputPremultiplied && rgba[3] > 1.0e-8F) {
    for (std::size_t channel = 0; channel < 3; ++channel) rgba[channel] /= rgba[3];
  }
  return rgba;
}

std::array<float, 4> sample(const ImageView& source, double x, double y,
                            double scaleX, double scaleY,
                            ResampleFilter filter, bool sourcePremultiplied,
                            bool outputPremultiplied) noexcept {
  std::array<double, 4> sum{};
  double weightSum = 0.0;
  const double radius = filter == ResampleFilter::Bilinear ? 1.0
                      : filter == ResampleFilter::Bicubic ? 2.0 : 3.0;
  const double filterScaleX = std::max(1.0, 1.0 / std::abs(scaleX));
  const double filterScaleY = std::max(1.0, 1.0 / std::abs(scaleY));
  const int firstX = static_cast<int>(std::ceil(x - radius * filterScaleX));
  const int lastX = static_cast<int>(std::floor(x + radius * filterScaleX));
  const int firstY = static_cast<int>(std::ceil(y - radius * filterScaleY));
  const int lastY = static_cast<int>(std::floor(y + radius * filterScaleY));
  for (int iy = firstY; iy <= lastY; ++iy) {
    const double wy = kernel((y - static_cast<double>(iy)) / filterScaleY, filter);
    if (wy == 0.0) continue;
    const int sy = std::clamp(iy, source.bounds.y1, source.bounds.y2 - 1);
    for (int ix = firstX; ix <= lastX; ++ix) {
      const double weight = wy * kernel(
          (x - static_cast<double>(ix)) / filterScaleX, filter);
      if (weight == 0.0) continue;
      const int sx = std::clamp(ix, source.bounds.x1, source.bounds.x2 - 1);
      const auto rgba = readRGBA(source, sx, sy, sourcePremultiplied, true);
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
      const double sampleX = sourceX - 0.5;
      const double sampleY = sourceY - 0.5;
      const bool exactIdentity = options.placement == PlacementMode::Identity;
      if (exactIdentity) {
        writePixel(destination, x, y,
                   readRGBA(source, x, y, options.sourcePremultiplied,
                            options.outputPremultiplied));
      } else {
        writePixel(destination, x, y,
                   sample(source, sampleX, sampleY, transform.scaleX, transform.scaleY,
                          options.filter, options.sourcePremultiplied,
                          options.outputPremultiplied));
      }
    }
  }
}

RectD computeBlankingAperture(RectI outputBounds,
                              const BlankingOptions& options) noexcept {
  const double width = std::max(0, outputBounds.x2 - outputBounds.x1);
  const double height = std::max(0, outputBounds.y2 - outputBounds.y1);
  if (width == 0.0 || height == 0.0 || options.editorialAspect <= 0.0) {
    return {static_cast<double>(outputBounds.x1), static_cast<double>(outputBounds.y1),
            static_cast<double>(outputBounds.x2), static_cast<double>(outputBounds.y2)};
  }
  const double outputPAR = options.outputPixelAspect > 0.0
      ? options.outputPixelAspect : 1.0;
  const double canvasAspect = width * outputPAR / height;
  RectD aperture{static_cast<double>(outputBounds.x1),
                 static_cast<double>(outputBounds.y1),
                 static_cast<double>(outputBounds.x2),
                 static_cast<double>(outputBounds.y2)};
  if (options.editorialAspect > canvasAspect) {
    const double apertureHeight = width * outputPAR / options.editorialAspect;
    const double centre = (outputBounds.y1 + outputBounds.y2) * 0.5;
    aperture.y1 = centre - apertureHeight * 0.5;
    aperture.y2 = centre + apertureHeight * 0.5;
  } else if (options.editorialAspect < canvasAspect) {
    const double apertureWidth = height * options.editorialAspect / outputPAR;
    const double centre = (outputBounds.x1 + outputBounds.x2) * 0.5;
    aperture.x1 = centre - apertureWidth * 0.5;
    aperture.x2 = centre + apertureWidth * 0.5;
  }
  return aperture;
}

void applyBlanking(const ImageView& destination, RectI renderWindow,
                   const BlankingOptions& options) noexcept {
  if (!options.enabled || !destination.data || destination.pixelBytes == 0 ||
      destination.channels <= 0 || options.editorialAspect <= 0.0) return;
  const RectI writable = intersect(renderWindow, destination.bounds);
  if (empty(writable)) return;
  const RectD aperture = computeBlankingAperture(destination.bounds, options);
  const float colourAlpha = std::clamp(options.colour[3], 0.0F, 1.0F);
  const float opacity = std::clamp(options.opacity, 0.0F, 1.0F);
  for (int y = writable.y1; y < writable.y2; ++y) {
    const double overlapY = std::max(0.0, std::min(static_cast<double>(y + 1), aperture.y2)
                                         - std::max(static_cast<double>(y), aperture.y1));
    for (int x = writable.x1; x < writable.x2; ++x) {
      const double overlapX = std::max(0.0, std::min(static_cast<double>(x + 1), aperture.x2)
                                           - std::max(static_cast<double>(x), aperture.x1));
      const float coverage = static_cast<float>(1.0 - overlapX * overlapY);
      const float alpha = std::clamp(coverage * opacity * colourAlpha, 0.0F, 1.0F);
      if (alpha <= 0.0F) continue;
      auto base = readRGBA(destination, x, y, options.outputPremultiplied, true);
      std::array<float, 4> result{};
      for (std::size_t channel = 0; channel < 3; ++channel) {
        result[channel] = options.colour[channel] * alpha + base[channel] * (1.0F - alpha);
      }
      result[3] = alpha + base[3] * (1.0F - alpha);
      if (!options.outputPremultiplied && result[3] > 1.0e-8F) {
        for (std::size_t channel = 0; channel < 3; ++channel) result[channel] /= result[3];
      }
      writePixel(destination, x, y, result);
    }
  }
}

PointI computeTextOrigin(RectI outputBounds, int maskWidth, int maskHeight,
                         const TextOverlayOptions& options) noexcept {
  const int width = std::max(0, outputBounds.x2 - outputBounds.x1);
  const int height = std::max(0, outputBounds.y2 - outputBounds.y1);
  const int left = options.constrainToCell
      ? options.cellBounds.x1
      : outputBounds.x1 + static_cast<int>(std::lround(options.paddingLeft * width));
  const int right = options.constrainToCell
      ? options.cellBounds.x2
      : outputBounds.x2 - static_cast<int>(std::lround(options.paddingRight * width));
  const int bottom = outputBounds.y1 + static_cast<int>(std::lround(options.paddingBottom * height));
  const int top = outputBounds.y2 - static_cast<int>(std::lround(options.paddingTop * height));

  PointI origin;
  switch (options.anchor) {
    case TextAnchor::TopLeft:
    case TextAnchor::BottomLeft:
      origin.x = left;
      break;
    case TextAnchor::TopCenter:
    case TextAnchor::BottomCenter:
      origin.x = options.constrainToCell
          ? left + (right - left - maskWidth) / 2
          : outputBounds.x1 + (width - maskWidth) / 2;
      break;
    case TextAnchor::TopRight:
    case TextAnchor::BottomRight:
      origin.x = right - maskWidth;
      break;
  }
  switch (options.anchor) {
    case TextAnchor::TopLeft:
    case TextAnchor::TopCenter:
    case TextAnchor::TopRight:
      origin.y = top - maskHeight;
      break;
    case TextAnchor::BottomLeft:
    case TextAnchor::BottomCenter:
    case TextAnchor::BottomRight:
      origin.y = bottom;
      break;
  }
  origin.x += static_cast<int>(std::lround(options.offsetX * width));
  origin.y += static_cast<int>(std::lround(options.offsetY * height));
  return origin;
}

RectI computeTextCell(RectI outputBounds, TextAnchor anchor,
                      double paddingLeft, double paddingRight,
                      double zoneGap) noexcept {
  const int width = std::max(0, outputBounds.x2 - outputBounds.x1);
  const double left = static_cast<double>(outputBounds.x1) +
      std::clamp(paddingLeft, 0.0, 1.0) * width;
  const double right = static_cast<double>(outputBounds.x2) -
      std::clamp(paddingRight, 0.0, 1.0) * width;
  const double firstThird = static_cast<double>(outputBounds.x1) + width / 3.0;
  const double secondThird = static_cast<double>(outputBounds.x1) + 2.0 * width / 3.0;
  const double halfGap = std::clamp(zoneGap, 0.0, 1.0) * width * 0.5;

  RectI cell{outputBounds.x1, outputBounds.y1, outputBounds.x2, outputBounds.y2};
  switch (anchor) {
    case TextAnchor::TopLeft:
    case TextAnchor::BottomLeft:
      cell.x1 = static_cast<int>(std::lround(left));
      cell.x2 = static_cast<int>(std::lround(firstThird - halfGap));
      break;
    case TextAnchor::TopCenter:
    case TextAnchor::BottomCenter:
      cell.x1 = static_cast<int>(std::lround(firstThird + halfGap));
      cell.x2 = static_cast<int>(std::lround(secondThird - halfGap));
      break;
    case TextAnchor::TopRight:
    case TextAnchor::BottomRight:
      cell.x1 = static_cast<int>(std::lround(secondThird + halfGap));
      cell.x2 = static_cast<int>(std::lround(right));
      break;
  }
  cell.x1 = std::clamp(cell.x1, outputBounds.x1, outputBounds.x2);
  cell.x2 = std::clamp(cell.x2, cell.x1, outputBounds.x2);
  return cell;
}

void compositeTextMask(const ImageView& destination, RectI renderWindow,
                       const GlyphMaskView& mask,
                       const TextOverlayOptions& options) noexcept {
  if (!options.enabled || !destination.data || destination.pixelBytes == 0 ||
      destination.channels <= 0 || !mask.data || mask.width <= 0 ||
      mask.height <= 0 || mask.rowBytes == 0) return;
  RectI writable = intersect(renderWindow, destination.bounds);
  if (options.constrainToCell) writable = intersect(writable, options.cellBounds);
  if (empty(writable)) return;
  const PointI origin = computeTextOrigin(
      destination.bounds, mask.width, mask.height, options);
  const RectI maskBounds{origin.x, origin.y, origin.x + mask.width, origin.y + mask.height};
  const RectI area = intersect(writable, maskBounds);
  if (empty(area)) return;
  const float colourAlpha = std::clamp(options.colour[3], 0.0F, 1.0F);
  const float opacity = std::clamp(options.opacity, 0.0F, 1.0F);
  for (int y = area.y1; y < area.y2; ++y) {
    const int maskY = y - origin.y;
    const auto* maskRow = mask.data + static_cast<std::ptrdiff_t>(maskY) * mask.rowBytes;
    for (int x = area.x1; x < area.x2; ++x) {
      const float coverage = static_cast<float>(maskRow[x - origin.x]) / 255.0F;
      const float alpha = coverage * colourAlpha * opacity;
      if (alpha <= 0.0F) continue;
      auto base = readRGBA(destination, x, y, options.outputPremultiplied, true);
      std::array<float, 4> result{};
      for (std::size_t channel = 0; channel < 3; ++channel) {
        result[channel] = options.colour[channel] * alpha + base[channel] * (1.0F - alpha);
      }
      result[3] = alpha + base[3] * (1.0F - alpha);
      if (!options.outputPremultiplied && result[3] > 1.0e-8F) {
        for (std::size_t channel = 0; channel < 3; ++channel) result[channel] /= result[3];
      }
      writePixel(destination, x, y, result);
    }
  }
}

}  // namespace wipreview::probe
