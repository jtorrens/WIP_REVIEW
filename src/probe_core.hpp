#pragma once

#include "color_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

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

enum class ReviewRasterPreset {
  HD,
  UHD,
  DCI2K,
  DCI4K,
  Custom,
};

struct RasterSize {
  int width = 1920;
  int height = 1080;
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

struct ManagedRenderStats {
  std::size_t decodedRows = 0;
  std::size_t peakCachedRows = 0;
  std::size_t peakCacheBytes = 0;
};

// Tracks localized working pixels and the tight dirty span of every row. The
// bitmap prevents repeated transfer decoding where overlays overlap; row spans
// let the final encode skip untouched portions of the raster entirely.
class ManagedDirtyRegion {
 public:
  explicit ManagedDirtyRegion(RectI bounds);

  [[nodiscard]] bool mark(int x, int y) noexcept;
  [[nodiscard]] bool contains(int x, int y) const noexcept;
  [[nodiscard]] int rowBegin(int y) const noexcept;
  [[nodiscard]] int rowEnd(int y) const noexcept;
  [[nodiscard]] std::size_t count() const noexcept { return count_; }

 private:
  RectI bounds_{};
  int width_ = 0;
  std::vector<std::uint8_t> pixels_;
  std::vector<int> rowBegin_;
  std::vector<int> rowEnd_;
  std::size_t count_ = 0;
};

[[nodiscard]] RectI intersect(RectI a, RectI b) noexcept;
[[nodiscard]] bool empty(RectI rect) noexcept;

[[nodiscard]] RasterSize resolveReviewRaster(
    ReviewRasterPreset preset, int customWidth, int customHeight) noexcept;

[[nodiscard]] PlacementTransform computePlacement(
    RectI sourceBounds,
    RectI outputBounds,
    const RenderOptions& options) noexcept;

// True when the selected placement maps every output pixel to the same Source
// coordinate. This includes Fit/Fill/Stretch/1:1 when their resolved transform
// is exactly 1:1, not only the explicit Identity mode.
[[nodiscard]] bool isEffectivelyIdentityPlacement(
    RectI sourceBounds,
    RectI outputBounds,
    const RenderOptions& options) noexcept;

// Copies a coordinate-aligned render window without colour conversion. The
// fast path requires matching representations and alpha conventions.
[[nodiscard]] bool copyIdentityFrame(
    const ImageView& source,
    const ImageView& destination,
    RectI renderWindow,
    bool sourcePremultiplied,
    bool outputPremultiplied) noexcept;

// Renders a static formatter pass into renderWindow. Pixels outside the placed
// source are filled with canvas. Identity is coordinate-aligned and never
// introduces a resize; all other modes are centred in the output bounds.
void renderStaticFrame(const ImageView& source,
                       const ImageView& destination,
                       RectI renderWindow,
                       const RenderOptions& options);

// Decodes display-referred Source pixels exactly once into display-light
// linear premultiplied Float32 RGBA. Resampling is a separate pass over this
// decoded surface, so filter taps never repeat PQ/HLG transfer functions.
void decodeManagedDisplayFrame(
    const ImageView& source,
    const ImageView& displayLinearDestination,
    RectI renderWindow,
    bool sourcePremultiplied,
    const wipreview::color::DisplayConfig& colorConfig) noexcept;

// Executes the managed decode and placement pass. Identity decodes directly
// into the destination. Resampled placements use a bounded decoded-row cache,
// so transfer functions execute once per required Source row without a
// full-raster scratch image.
[[nodiscard]] bool renderManagedDisplayFrame(
    const ImageView& source,
    const ImageView& displayLinearDestination,
    RectI renderWindow,
    const RenderOptions& options,
    const wipreview::color::DisplayConfig& colorConfig,
    ManagedRenderStats* stats = nullptr);

// Encodes the premultiplied display-light working image exactly once into the
// negotiated Output representation, preserving its alpha convention.
void encodeManagedDisplayFrame(
    const ImageView& displayLinearSource,
    const ImageView& destination,
    RectI renderWindow,
    const wipreview::color::DisplayConfig& colorConfig,
    bool outputPremultiplied) noexcept;

// Fused identity blanking pass. Pixels reserved by the text dirty region stay
// in the shared display-light workspace so blanking and text need one final
// encode. Every other affected pixel is decoded, composited and encoded
// directly to Output. Disjoint render-window bands may run in parallel.
void compositeManagedIdentityBlankingFused(
    const ImageView& encodedOutput,
    const ImageView& displayLinearWorkspace,
    const ManagedDirtyRegion& textDirtyRegion,
    RectI renderWindow,
    const BlankingOptions& options,
    const wipreview::color::DisplayConfig& colorConfig,
    bool outputPremultiplied) noexcept;

void prepareManagedTextPixels(
    const ImageView& encodedBase,
    const ImageView& displayLinearWorkspace,
    ManagedDirtyRegion& dirtyRegion,
    RectI renderWindow,
    const GlyphMaskView& mask,
    const TextOverlayOptions& options,
    const wipreview::color::DisplayConfig& colorConfig,
    bool basePremultiplied) noexcept;

void encodeManagedDirtyPixels(
    const ImageView& displayLinearWorkspace,
    const ImageView& destination,
    const ManagedDirtyRegion& dirtyRegion,
    RectI renderWindow,
    const wipreview::color::DisplayConfig& colorConfig,
    bool outputPremultiplied) noexcept;

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
