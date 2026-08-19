#pragma once

#include <cstddef>
#include <cstdint>

namespace wipreview::probe {

struct RectI {
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
};

enum class ChannelType {
  UInt8,
  UInt16,
  Half,
  Float32,
};

enum class PlacementMode {
  Identity,
  Fit,
  Fill,
  Stretch,
  OneToOne,
};

enum class ResampleFilter {
  Bilinear,
  Bicubic,
  Lanczos3,
};

struct ImageView {
  std::byte* data = nullptr;
  RectI bounds{};
  std::ptrdiff_t rowBytes = 0;
  std::size_t pixelBytes = 0;
  int channels = 0;
  ChannelType channelType = ChannelType::UInt8;
};

struct RenderOptions {
  PlacementMode placement = PlacementMode::Fit;
  ResampleFilter filter = ResampleFilter::Lanczos3;
  double sourcePixelAspect = 1.0;
  double outputPixelAspect = 1.0;
  bool sourcePremultiplied = true;
  bool outputPremultiplied = true;
  float canvas[4] = {0.0F, 0.0F, 0.0F, 1.0F};
};

struct PlacementTransform {
  double scaleX = 1.0;
  double scaleY = 1.0;
  double sourceCenterX = 0.0;
  double sourceCenterY = 0.0;
  double outputCenterX = 0.0;
  double outputCenterY = 0.0;
};

struct RectD {
  double x1 = 0.0;
  double y1 = 0.0;
  double x2 = 0.0;
  double y2 = 0.0;
};

struct BlankingOptions {
  bool enabled = false;
  double editorialAspect = 2.0;
  double outputPixelAspect = 1.0;
  bool outputPremultiplied = true;
  float colour[4] = {0.0F, 0.0F, 0.0F, 1.0F};
  float opacity = 1.0F;
};

enum class TextAnchor {
  TopLeft,
  TopCenter,
  TopRight,
  BottomLeft,
  BottomCenter,
  BottomRight,
};

struct GlyphMaskView {
  const std::uint8_t* data = nullptr;
  int width = 0;
  int height = 0;
  std::ptrdiff_t rowBytes = 0;
};

struct TextOverlayOptions {
  bool enabled = false;
  TextAnchor anchor = TextAnchor::TopLeft;
  double paddingLeft = 0.015;
  double paddingRight = 0.015;
  double paddingTop = 0.020;
  double paddingBottom = 0.020;
  double offsetX = 0.0;
  double offsetY = 0.0;
  bool outputPremultiplied = true;
  float colour[4] = {1.0F, 1.0F, 1.0F, 1.0F};
  float opacity = 1.0F;
};

struct PointI {
  int x = 0;
  int y = 0;
};

[[nodiscard]] RectI intersect(RectI a, RectI b) noexcept;
[[nodiscard]] bool empty(RectI rect) noexcept;

// Clears the requested destination window, then copies the coordinate-aligned
// source intersection when the two formats have the same pixel size. This is
// deliberately not a resize: P0 must observe host geometry, not hide it.
void copyProbeFrame(const ImageView& source,
                    const ImageView& destination,
                    RectI renderWindow) noexcept;

[[nodiscard]] PlacementTransform computePlacement(
    RectI sourceBounds,
    RectI outputBounds,
    const RenderOptions& options) noexcept;

// Renders a static formatter pass into renderWindow. Pixels outside the placed
// source are filled with canvas. Identity is coordinate-aligned and never
// introduces a resize; all other modes are centred in the output bounds.
void renderStaticFrame(const ImageView& source,
                       const ImageView& destination,
                       RectI renderWindow,
                       const RenderOptions& options) noexcept;

[[nodiscard]] RectD computeBlankingAperture(
    RectI outputBounds,
    const BlankingOptions& options) noexcept;

// Composites the editorial blanking outside the centred aperture. Fractional
// aperture edges use pixel-area coverage. The destination is interpreted and
// written according to outputPremultiplied.
void applyBlanking(const ImageView& destination,
                   RectI renderWindow,
                   const BlankingOptions& options) noexcept;

[[nodiscard]] PointI computeTextOrigin(
    RectI outputBounds,
    int maskWidth,
    int maskHeight,
    const TextOverlayOptions& options) noexcept;

void compositeTextMask(const ImageView& destination,
                       RectI renderWindow,
                       const GlyphMaskView& mask,
                       const TextOverlayOptions& options) noexcept;

}  // namespace wipreview::probe
