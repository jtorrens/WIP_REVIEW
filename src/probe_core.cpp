#include "probe_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <vector>

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

std::array<float, 4> readManagedRGBA(
    const ImageView& source, int x, int y, bool sourcePremultiplied,
    const wipreview::color::DisplayConfig& colorConfig) noexcept {
  const std::byte* pixel = pixelAddress(source, x, y);
  std::array<float, 4> rgba{0.0F, 0.0F, 0.0F, 1.0F};
  if (source.channels == 1) {
    rgba[3] = readChannel(pixel, 0, source.channelType);
    return rgba;
  }
  for (int channel = 0; channel < source.channels; ++channel) {
    rgba[static_cast<std::size_t>(channel)] =
        readChannel(pixel, channel, source.channelType);
  }
  if (sourcePremultiplied) {
    if (rgba[3] <= 1.0e-8F) {
      rgba[0] = rgba[1] = rgba[2] = 0.0F;
      return rgba;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      rgba[channel] /= rgba[3];
    }
  }
  const auto decoded = wipreview::color::decodeDisplay(
      {rgba[0], rgba[1], rgba[2]}, colorConfig);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    rgba[channel] = decoded[channel] * rgba[3];
  }
  return rgba;
}

struct AxisTap {
  int source = 0;
  double weight = 0.0;
};

struct AxisSpan {
  std::size_t first = 0;
  std::size_t count = 0;
  bool inside = false;
};

struct AxisPlan {
  int outputStart = 0;
  std::vector<AxisSpan> spans;
  std::vector<AxisTap> taps;
};

AxisPlan buildAxisPlan(int outputStart, int outputEnd,
                       int sourceStart, int sourceEnd,
                       double sourceCenter, double outputCenter,
                       double scale, ResampleFilter filter) {
  AxisPlan plan;
  plan.outputStart = outputStart;
  plan.spans.reserve(static_cast<std::size_t>(
      std::max(0, outputEnd - outputStart)));
  const double radius = filter == ResampleFilter::Bilinear ? 1.0
                      : filter == ResampleFilter::Bicubic ? 2.0 : 3.0;
  const double filterScale = std::max(1.0, 1.0 / std::abs(scale));
  const std::size_t estimatedTaps = static_cast<std::size_t>(
      std::max(2.0, std::ceil(radius * filterScale * 2.0 + 1.0)));
  plan.taps.reserve(plan.spans.capacity() * estimatedTaps);
  for (int output = outputStart; output < outputEnd; ++output) {
    const double canonical = sourceCenter +
        (static_cast<double>(output) + 0.5 - outputCenter) / scale;
    AxisSpan span;
    span.first = plan.taps.size();
    span.inside = canonical >= sourceStart && canonical < sourceEnd;
    if (span.inside) {
      const double samplePosition = canonical - 0.5;
      const int first = static_cast<int>(
          std::ceil(samplePosition - radius * filterScale));
      const int last = static_cast<int>(
          std::floor(samplePosition + radius * filterScale));
      for (int sampleIndex = first; sampleIndex <= last; ++sampleIndex) {
        const double weight = kernel(
            (samplePosition - static_cast<double>(sampleIndex)) / filterScale,
            filter);
        if (weight == 0.0) continue;
        plan.taps.push_back({
            std::clamp(sampleIndex, sourceStart, sourceEnd - 1), weight});
      }
    }
    span.count = plan.taps.size() - span.first;
    plan.spans.push_back(span);
  }
  return plan;
}

const AxisSpan& spanAt(const AxisPlan& plan, int output) noexcept {
  return plan.spans[static_cast<std::size_t>(output - plan.outputStart)];
}

std::array<float, 4> samplePlanned(
    const ImageView& source, const AxisPlan& xPlan, const AxisPlan& yPlan,
    int outputX, int outputY, bool sourcePremultiplied,
    bool outputPremultiplied) noexcept {
  const AxisSpan& xSpan = spanAt(xPlan, outputX);
  const AxisSpan& ySpan = spanAt(yPlan, outputY);
  std::array<double, 4> sum{};
  double weightSum = 0.0;
  for (std::size_t yIndex = 0; yIndex < ySpan.count; ++yIndex) {
    const AxisTap& yTap = yPlan.taps[ySpan.first + yIndex];
    for (std::size_t xIndex = 0; xIndex < xSpan.count; ++xIndex) {
      const AxisTap& xTap = xPlan.taps[xSpan.first + xIndex];
      const double weight = yTap.weight * xTap.weight;
      const auto rgba = readRGBA(
          source, xTap.source, yTap.source, sourcePremultiplied, true);
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
    for (std::size_t channel = 0; channel < 3; ++channel) {
      result[channel] /= result[3];
    }
  }
  return result;
}

struct DecodedRow {
  int sourceY = 0;
  std::vector<std::array<float, 4>> pixels;
};

class ManagedRowCache {
 public:
  ManagedRowCache(const ImageView& source, bool sourcePremultiplied,
                  const wipreview::color::DisplayConfig& colorConfig,
                  std::size_t capacity)
      : source_(source), sourcePremultiplied_(sourcePremultiplied),
        colorConfig_(colorConfig), capacity_(std::max<std::size_t>(1, capacity)) {}

  const std::array<float, 4>* get(int sourceY) {
    for (const auto& row : rows_) {
      if (row.sourceY == sourceY) return row.pixels.data();
    }
    if (rows_.size() >= capacity_) rows_.pop_front();
    DecodedRow row;
    row.sourceY = sourceY;
    const int width = source_.bounds.x2 - source_.bounds.x1;
    row.pixels.resize(static_cast<std::size_t>(width));
    for (int x = source_.bounds.x1; x < source_.bounds.x2; ++x) {
      row.pixels[static_cast<std::size_t>(x - source_.bounds.x1)] =
          readManagedRGBA(
              source_, x, sourceY, sourcePremultiplied_, colorConfig_);
    }
    rows_.push_back(std::move(row));
    ++decodedRows_;
    peakRows_ = std::max(peakRows_, rows_.size());
    return rows_.back().pixels.data();
  }

  [[nodiscard]] std::size_t decodedRows() const noexcept { return decodedRows_; }
  [[nodiscard]] std::size_t peakRows() const noexcept { return peakRows_; }

 private:
  const ImageView& source_;
  bool sourcePremultiplied_ = true;
  const wipreview::color::DisplayConfig& colorConfig_;
  std::size_t capacity_ = 1;
  std::deque<DecodedRow> rows_;
  std::size_t decodedRows_ = 0;
  std::size_t peakRows_ = 0;
};

std::array<float, 4> sampleManagedRows(
    const ImageView& source, const AxisPlan& xPlan, const AxisPlan& yPlan,
    int outputX, int outputY,
    const std::vector<const std::array<float, 4>*>& decodedRows) noexcept {
  const AxisSpan& xSpan = spanAt(xPlan, outputX);
  const AxisSpan& ySpan = spanAt(yPlan, outputY);
  std::array<double, 4> sum{};
  double weightSum = 0.0;
  for (std::size_t yIndex = 0; yIndex < ySpan.count; ++yIndex) {
    const AxisTap& yTap = yPlan.taps[ySpan.first + yIndex];
    const auto* row = decodedRows[yIndex];
    for (std::size_t xIndex = 0; xIndex < xSpan.count; ++xIndex) {
      const AxisTap& xTap = xPlan.taps[xSpan.first + xIndex];
      const double weight = yTap.weight * xTap.weight;
      const auto& rgba = row[xTap.source - source.bounds.x1];
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
  return result;
}

}  // namespace

RasterSize resolveReviewRaster(ReviewRasterPreset preset, int customWidth,
                               int customHeight) noexcept {
  switch (preset) {
    case ReviewRasterPreset::HD:
      return {1920, 1080};
    case ReviewRasterPreset::UHD:
      return {3840, 2160};
    case ReviewRasterPreset::DCI2K:
      return {2048, 1080};
    case ReviewRasterPreset::DCI4K:
      return {4096, 2160};
    case ReviewRasterPreset::Custom:
      return {std::max(1, customWidth), std::max(1, customHeight)};
  }
  return {1920, 1080};
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

bool isEffectivelyIdentityPlacement(
    RectI sourceBounds, RectI outputBounds,
    const RenderOptions& options) noexcept {
  if (sourceBounds.x1 != outputBounds.x1 ||
      sourceBounds.y1 != outputBounds.y1 ||
      sourceBounds.x2 != outputBounds.x2 ||
      sourceBounds.y2 != outputBounds.y2) return false;
  const PlacementTransform transform = computePlacement(
      sourceBounds, outputBounds, options);
  constexpr double tolerance = 1.0e-12;
  return std::abs(transform.scaleX - 1.0) <= tolerance &&
      std::abs(transform.scaleY - 1.0) <= tolerance &&
      std::abs(transform.sourceCenterX - transform.outputCenterX) <= tolerance &&
      std::abs(transform.sourceCenterY - transform.outputCenterY) <= tolerance;
}

bool copyIdentityFrame(
    const ImageView& source, const ImageView& destination,
    RectI renderWindow, bool sourcePremultiplied,
    bool outputPremultiplied) noexcept {
  if (!source.data || !destination.data ||
      source.channels != destination.channels ||
      source.channelType != destination.channelType ||
      source.pixelBytes != destination.pixelBytes ||
      sourcePremultiplied != outputPremultiplied) return false;
  const RectI writable = intersect(
      intersect(renderWindow, source.bounds), destination.bounds);
  if (empty(writable)) return true;
  const std::size_t rowSize = static_cast<std::size_t>(
      writable.x2 - writable.x1) * source.pixelBytes;
  for (int y = writable.y1; y < writable.y2; ++y) {
    std::memmove(pixelAddress(destination, writable.x1, y),
                 pixelAddress(source, writable.x1, y), rowSize);
  }
  return true;
}

void renderStaticFrame(const ImageView& source, const ImageView& destination,
                       RectI renderWindow, const RenderOptions& options) {
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
  const bool exactIdentity = isEffectivelyIdentityPlacement(
      source.bounds, destination.bounds, options);
  AxisPlan xPlan;
  AxisPlan yPlan;
  if (!exactIdentity) {
    xPlan = buildAxisPlan(
        writable.x1, writable.x2, source.bounds.x1, source.bounds.x2,
        transform.sourceCenterX, transform.outputCenterX,
        transform.scaleX, options.filter);
    yPlan = buildAxisPlan(
        writable.y1, writable.y2, source.bounds.y1, source.bounds.y2,
        transform.sourceCenterY, transform.outputCenterY,
        transform.scaleY, options.filter);
  }
  for (int y = writable.y1; y < writable.y2; ++y) {
    for (int x = writable.x1; x < writable.x2; ++x) {
      if (exactIdentity) {
        if (x < source.bounds.x1 || x >= source.bounds.x2 ||
            y < source.bounds.y1 || y >= source.bounds.y2) {
          writePixel(destination, x, y, canvas);
          continue;
        }
        writePixel(destination, x, y,
                   readRGBA(source, x, y, options.sourcePremultiplied,
                            options.outputPremultiplied));
      } else {
        if (!spanAt(xPlan, x).inside || !spanAt(yPlan, y).inside) {
          writePixel(destination, x, y, canvas);
          continue;
        }
        writePixel(destination, x, y,
                   samplePlanned(source, xPlan, yPlan, x, y,
                                 options.sourcePremultiplied,
                                 options.outputPremultiplied));
      }
    }
  }
}

void decodeManagedDisplayFrame(
    const ImageView& source, const ImageView& destination,
    RectI renderWindow, bool sourcePremultiplied,
    const wipreview::color::DisplayConfig& colorConfig) noexcept {
  if (!destination.data || destination.pixelBytes != sizeof(float) * 4 ||
      destination.channels != 4 ||
      destination.channelType != ChannelType::Float32) return;
  if (!source.data || source.pixelBytes == 0 || source.channels <= 0 ||
      empty(source.bounds)) return;
  const RectI writable = intersect(
      intersect(renderWindow, destination.bounds), source.bounds);
  if (empty(writable)) return;
  for (int y = writable.y1; y < writable.y2; ++y) {
    for (int x = writable.x1; x < writable.x2; ++x) {
      writePixel(destination, x, y, readManagedRGBA(
          source, x, y, sourcePremultiplied, colorConfig));
    }
  }
}

bool renderManagedDisplayFrame(
    const ImageView& source, const ImageView& destination, RectI renderWindow,
    const RenderOptions& options,
    const wipreview::color::DisplayConfig& colorConfig,
    ManagedRenderStats* stats) {
  if (stats) *stats = {};
  if (!destination.data || destination.pixelBytes != sizeof(float) * 4 ||
      destination.channels != 4 ||
      destination.channelType != ChannelType::Float32) return false;
  const RectI writable = intersect(renderWindow, destination.bounds);
  if (empty(writable)) return true;

  const bool sourceAvailable = source.data && source.pixelBytes > 0 &&
      source.channels > 0 && !empty(source.bounds);
  const RectI identitySourceArea = intersect(writable, source.bounds);
  const bool directIdentityDecode = sourceAvailable &&
      isEffectivelyIdentityPlacement(source.bounds, destination.bounds,
                                     options) &&
      identitySourceArea.x1 == writable.x1 &&
      identitySourceArea.y1 == writable.y1 &&
      identitySourceArea.x2 == writable.x2 &&
      identitySourceArea.y2 == writable.y2;
  if (directIdentityDecode) {
    decodeManagedDisplayFrame(
        source, destination, writable, options.sourcePremultiplied, colorConfig);
    if (stats) stats->decodedRows = static_cast<std::size_t>(
        writable.y2 - writable.y1);
    return true;
  }

  auto linearOptions = options;
  linearOptions.sourcePremultiplied = true;
  linearOptions.outputPremultiplied = true;
  if (!sourceAvailable) {
    renderStaticFrame({}, destination, writable, linearOptions);
    return true;
  }

  const PlacementTransform transform = computePlacement(
      source.bounds, destination.bounds, options);
  const AxisPlan xPlan = buildAxisPlan(
      writable.x1, writable.x2, source.bounds.x1, source.bounds.x2,
      transform.sourceCenterX, transform.outputCenterX,
      transform.scaleX, options.filter);
  const AxisPlan yPlan = buildAxisPlan(
      writable.y1, writable.y2, source.bounds.y1, source.bounds.y2,
      transform.sourceCenterY, transform.outputCenterY,
      transform.scaleY, options.filter);
  std::size_t maximumYSpan = 1;
  for (const auto& span : yPlan.spans) {
    maximumYSpan = std::max(maximumYSpan, span.count);
  }
  ManagedRowCache cache(
      source, options.sourcePremultiplied, colorConfig, maximumYSpan * 2);
  std::vector<const std::array<float, 4>*> decodedRows;
  decodedRows.reserve(maximumYSpan);
  std::array<float, 4> canvas{
      options.canvas[0] * options.canvas[3],
      options.canvas[1] * options.canvas[3],
      options.canvas[2] * options.canvas[3],
      options.canvas[3]};
  for (int y = writable.y1; y < writable.y2; ++y) {
    const AxisSpan& ySpan = spanAt(yPlan, y);
    decodedRows.clear();
    if (ySpan.inside) {
      for (std::size_t yIndex = 0; yIndex < ySpan.count; ++yIndex) {
        const AxisTap& yTap = yPlan.taps[ySpan.first + yIndex];
        decodedRows.push_back(cache.get(yTap.source));
      }
    }
    for (int x = writable.x1; x < writable.x2; ++x) {
      if (!ySpan.inside || !spanAt(xPlan, x).inside) {
        writePixel(destination, x, y, canvas);
        continue;
      }
      writePixel(destination, x, y, sampleManagedRows(
          source, xPlan, yPlan, x, y, decodedRows));
    }
  }
  if (stats) {
    stats->decodedRows = cache.decodedRows();
    stats->peakCachedRows = cache.peakRows();
    const std::size_t sourceWidth = static_cast<std::size_t>(
        source.bounds.x2 - source.bounds.x1);
    stats->peakCacheBytes = cache.peakRows() * sourceWidth *
        sizeof(std::array<float, 4>);
  }
  return true;
}

void encodeManagedDisplayFrame(
    const ImageView& source, const ImageView& destination,
    RectI renderWindow, const wipreview::color::DisplayConfig& colorConfig,
    bool outputPremultiplied) noexcept {
  if (!source.data || source.pixelBytes != sizeof(float) * 4 ||
      source.channels != 4 || source.channelType != ChannelType::Float32 ||
      !destination.data || destination.pixelBytes == 0 ||
      destination.channels <= 0) return;
  const RectI writable = intersect(
      intersect(renderWindow, source.bounds), destination.bounds);
  for (int y = writable.y1; y < writable.y2; ++y) {
    for (int x = writable.x1; x < writable.x2; ++x) {
      auto rgba = readRGBA(source, x, y, true, true);
      const float alpha = rgba[3];
      wipreview::color::RGB straight{};
      if (alpha > 1.0e-8F) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
          straight[channel] = rgba[channel] / alpha;
        }
      }
      const auto encoded = wipreview::color::encodeDisplay(straight, colorConfig);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        rgba[channel] = outputPremultiplied ? encoded[channel] * alpha
                                           : encoded[channel];
      }
      writePixel(destination, x, y, rgba);
    }
  }
}

namespace {

template <typename Function>
void forEachBlankingPixel(RectI writable, RectI bounds, RectD aperture,
                          Function&& function) noexcept {
  const bool horizontalBands = aperture.y1 > bounds.y1 ||
      aperture.y2 < bounds.y2;
  if (horizontalBands) {
    for (int y = writable.y1; y < writable.y2; ++y) {
      const double overlap = std::max(
          0.0, std::min(static_cast<double>(y + 1), aperture.y2) -
                   std::max(static_cast<double>(y), aperture.y1));
      const float coverage = static_cast<float>(1.0 - overlap);
      if (coverage <= 0.0F) continue;
      for (int x = writable.x1; x < writable.x2; ++x) {
        function(x, y, coverage);
      }
    }
    return;
  }
  for (int x = writable.x1; x < writable.x2; ++x) {
    const double overlap = std::max(
        0.0, std::min(static_cast<double>(x + 1), aperture.x2) -
                 std::max(static_cast<double>(x), aperture.x1));
    const float coverage = static_cast<float>(1.0 - overlap);
    if (coverage <= 0.0F) continue;
    for (int y = writable.y1; y < writable.y2; ++y) {
      function(x, y, coverage);
    }
  }
}

void prepareManagedPixel(
    const ImageView& encodedBase, const ImageView& workspace,
    ManagedDirtyRegion& dirtyRegion,
    int x, int y, const wipreview::color::DisplayConfig& colorConfig,
    bool basePremultiplied) noexcept {
  if (!dirtyRegion.mark(x, y)) return;
  writePixel(workspace, x, y, readManagedRGBA(
      encodedBase, x, y, basePremultiplied, colorConfig));
}

}  // namespace

ManagedDirtyRegion::ManagedDirtyRegion(RectI bounds)
    : bounds_(bounds),
      width_(std::max(0, bounds.x2 - bounds.x1)),
      pixels_(static_cast<std::size_t>(width_) *
              static_cast<std::size_t>(std::max(0, bounds.y2 - bounds.y1))),
      rowBegin_(static_cast<std::size_t>(std::max(0, bounds.y2 - bounds.y1)),
                bounds.x2),
      rowEnd_(static_cast<std::size_t>(std::max(0, bounds.y2 - bounds.y1)),
              bounds.x1) {}

bool ManagedDirtyRegion::mark(int x, int y) noexcept {
  if (x < bounds_.x1 || x >= bounds_.x2 || y < bounds_.y1 || y >= bounds_.y2) {
    return false;
  }
  const std::size_t row = static_cast<std::size_t>(y - bounds_.y1);
  const std::size_t index = row * static_cast<std::size_t>(width_) +
      static_cast<std::size_t>(x - bounds_.x1);
  if (pixels_[index] != 0) return false;
  pixels_[index] = 1;
  rowBegin_[row] = std::min(rowBegin_[row], x);
  rowEnd_[row] = std::max(rowEnd_[row], x + 1);
  ++count_;
  return true;
}

bool ManagedDirtyRegion::contains(int x, int y) const noexcept {
  if (x < bounds_.x1 || x >= bounds_.x2 || y < bounds_.y1 || y >= bounds_.y2) {
    return false;
  }
  const std::size_t row = static_cast<std::size_t>(y - bounds_.y1);
  const std::size_t index = row * static_cast<std::size_t>(width_) +
      static_cast<std::size_t>(x - bounds_.x1);
  return pixels_[index] != 0;
}

int ManagedDirtyRegion::rowBegin(int y) const noexcept {
  if (y < bounds_.y1 || y >= bounds_.y2) return bounds_.x2;
  return rowBegin_[static_cast<std::size_t>(y - bounds_.y1)];
}

int ManagedDirtyRegion::rowEnd(int y) const noexcept {
  if (y < bounds_.y1 || y >= bounds_.y2) return bounds_.x1;
  return rowEnd_[static_cast<std::size_t>(y - bounds_.y1)];
}

void compositeManagedIdentityBlanking(
    const ImageView& encodedOutput, const ImageView& workspace,
    ManagedDirtyRegion& dirtyRegion,
    RectI renderWindow, const BlankingOptions& options,
    const wipreview::color::DisplayConfig& colorConfig,
    bool basePremultiplied) noexcept {
  if (!options.enabled || !encodedOutput.data || !workspace.data ||
      options.editorialAspect <= 0.0) return;
  const RectI writable = intersect(
      intersect(renderWindow, encodedOutput.bounds), workspace.bounds);
  if (empty(writable)) return;
  const RectD aperture = computeBlankingAperture(workspace.bounds, options);
  const float colourAlpha = std::clamp(options.colour[3], 0.0F, 1.0F);
  const float opacity = std::clamp(options.opacity, 0.0F, 1.0F);
  const auto encodedColour = wipreview::color::encodeDisplay(
      {options.colour[0], options.colour[1], options.colour[2]}, colorConfig);
  forEachBlankingPixel(writable, workspace.bounds, aperture,
      [&](int x, int y, float coverage) {
        const float alpha = std::clamp(
            coverage * opacity * colourAlpha, 0.0F, 1.0F);
        if (alpha <= 0.0F) return;
        if (alpha >= 1.0F) {
          writePixel(encodedOutput, x, y,
                     {encodedColour[0], encodedColour[1], encodedColour[2],
                      1.0F});
          return;
        }
        prepareManagedPixel(encodedOutput, workspace, dirtyRegion, x, y,
                            colorConfig, basePremultiplied);
        const auto base = readRGBA(workspace, x, y, true, true);
        std::array<float, 4> result{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
          result[channel] = options.colour[channel] * alpha +
              base[channel] * (1.0F - alpha);
        }
        result[3] = alpha + base[3] * (1.0F - alpha);
        writePixel(workspace, x, y, result);
      });
}

void prepareManagedTextPixels(
    const ImageView& encodedBase, const ImageView& workspace,
    ManagedDirtyRegion& dirtyRegion,
    RectI renderWindow, const GlyphMaskView& mask,
    const TextOverlayOptions& options,
    const wipreview::color::DisplayConfig& colorConfig,
    bool basePremultiplied) noexcept {
  if (!options.enabled || !encodedBase.data || !workspace.data ||
      !mask.data || mask.width <= 0 || mask.height <= 0 ||
      mask.rowBytes == 0) return;
  RectI writable = intersect(
      intersect(renderWindow, encodedBase.bounds), workspace.bounds);
  if (options.constrainToCell) writable = intersect(writable, options.cellBounds);
  const PointI origin = computeTextOrigin(
      workspace.bounds, mask.width, mask.height, options);
  const RectI area = intersect(
      writable, {origin.x, origin.y, origin.x + mask.width,
                 origin.y + mask.height});
  if (empty(area)) return;
  const float colourAlpha = std::clamp(options.colour[3], 0.0F, 1.0F);
  const float opacity = std::clamp(options.opacity, 0.0F, 1.0F);
  for (int y = area.y1; y < area.y2; ++y) {
    const auto* row = mask.data +
        static_cast<std::ptrdiff_t>(y - origin.y) * mask.rowBytes;
    for (int x = area.x1; x < area.x2; ++x) {
      const float coverage = static_cast<float>(row[x - origin.x]) / 255.0F;
      if (coverage * colourAlpha * opacity <= 0.0F) continue;
      prepareManagedPixel(encodedBase, workspace, dirtyRegion, x, y,
                          colorConfig, basePremultiplied);
    }
  }
}

void encodeManagedDirtyPixels(
    const ImageView& workspace, const ImageView& destination,
    const ManagedDirtyRegion& dirtyRegion,
    RectI renderWindow, const wipreview::color::DisplayConfig& colorConfig,
    bool outputPremultiplied) noexcept {
  if (!workspace.data || !destination.data) return;
  const RectI writable = intersect(
      intersect(renderWindow, workspace.bounds), destination.bounds);
  for (int y = writable.y1; y < writable.y2; ++y) {
    const int x1 = std::max(writable.x1, dirtyRegion.rowBegin(y));
    const int x2 = std::min(writable.x2, dirtyRegion.rowEnd(y));
    for (int x = x1; x < x2; ++x) {
      if (!dirtyRegion.contains(x, y)) continue;
      auto rgba = readRGBA(workspace, x, y, true, true);
      const float alpha = rgba[3];
      wipreview::color::RGB straight{};
      if (alpha > 1.0e-8F) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
          straight[channel] = rgba[channel] / alpha;
        }
      }
      const auto encoded = wipreview::color::encodeDisplay(
          straight, colorConfig);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        rgba[channel] = outputPremultiplied ? encoded[channel] * alpha
                                           : encoded[channel];
      }
      writePixel(destination, x, y, rgba);
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
  forEachBlankingPixel(writable, destination.bounds, aperture,
      [&](int x, int y, float coverage) {
      const float alpha = std::clamp(coverage * opacity * colourAlpha, 0.0F, 1.0F);
      if (alpha <= 0.0F) return;
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
      });
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
