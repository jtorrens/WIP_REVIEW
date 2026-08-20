#include "probe_core.hpp"

#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using wipreview::probe::ChannelType;
using wipreview::probe::ImageView;
using wipreview::probe::PlacementMode;
using wipreview::probe::RectI;
using wipreview::probe::RenderOptions;
using wipreview::probe::TextAnchor;
using wipreview::probe::TextOverlayOptions;

ImageView floatView(std::vector<float>& pixels, int width, int height) {
  return {reinterpret_cast<std::byte*>(pixels.data()), {0, 0, width, height},
          static_cast<std::ptrdiff_t>(width * 4 * sizeof(float)),
          sizeof(float) * 4, 4, ChannelType::Float32};
}

void fillSource(std::vector<float>& pixels, int width, int height) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t offset = static_cast<std::size_t>((y * width + x) * 4);
      pixels[offset] = static_cast<float>((x * 13 + y * 3) % 31) / 30.0F;
      pixels[offset + 1] = static_cast<float>((x * 5 + y * 11) % 29) / 28.0F;
      pixels[offset + 2] = static_cast<float>((x * 7 + y * 17) % 23) / 22.0F;
      pixels[offset + 3] = 1.0F;
    }
  }
}

void testR03IntegratedAndUpstreamEquivalence() {
  constexpr int sourceWidth = 12;
  constexpr int sourceHeight = 8;
  constexpr int reviewWidth = 8;
  constexpr int reviewHeight = 4;
  std::vector<float> source(sourceWidth * sourceHeight * 4);
  std::vector<float> integrated(reviewWidth * reviewHeight * 4);
  std::vector<float> upstream(reviewWidth * reviewHeight * 4);
  std::vector<float> hostRaster(reviewWidth * reviewHeight * 4);
  fillSource(source, sourceWidth, sourceHeight);
  auto sourceView = floatView(source, sourceWidth, sourceHeight);
  auto integratedView = floatView(integrated, reviewWidth, reviewHeight);
  auto upstreamView = floatView(upstream, reviewWidth, reviewHeight);
  auto hostRasterView = floatView(hostRaster, reviewWidth, reviewHeight);

  RenderOptions placement;
  placement.placement = PlacementMode::Fill;
  placement.filter = wipreview::probe::ResampleFilter::Lanczos3;
  wipreview::probe::renderStaticFrame(
      sourceView, integratedView, integratedView.bounds, placement);
  wipreview::probe::renderStaticFrame(
      sourceView, upstreamView, upstreamView.bounds, placement);

  RenderOptions identity;
  identity.placement = PlacementMode::Identity;
  wipreview::probe::renderStaticFrame(
      upstreamView, hostRasterView, hostRasterView.bounds, identity);
  assert(integrated == hostRaster);
}

void testG01HdUhdNormalizedLayout() {
  TextOverlayOptions hd;
  hd.anchor = TextAnchor::TopLeft;
  TextOverlayOptions uhd = hd;
  const auto hdOrigin = wipreview::probe::computeTextOrigin(
      {0, 0, 1920, 1080}, 200, 40, hd);
  const auto uhdOrigin = wipreview::probe::computeTextOrigin(
      {0, 0, 3840, 2160}, 400, 80, uhd);
  assert(uhdOrigin.x == hdOrigin.x * 2);
  assert(std::abs(uhdOrigin.y - hdOrigin.y * 2) <= 1);

  wipreview::probe::BlankingOptions blanking;
  blanking.enabled = true;
  blanking.editorialAspect = 2.0;
  const auto hdAperture = wipreview::probe::computeBlankingAperture(
      {0, 0, 1920, 1080}, blanking);
  const auto uhdAperture = wipreview::probe::computeBlankingAperture(
      {0, 0, 3840, 2160}, blanking);
  assert(uhdAperture.x1 == hdAperture.x1 * 2.0);
  assert(uhdAperture.y1 == hdAperture.y1 * 2.0);
  assert(uhdAperture.x2 == hdAperture.x2 * 2.0);
  assert(uhdAperture.y2 == hdAperture.y2 * 2.0);
}

void testReviewRasterPresets() {
  using wipreview::probe::ReviewRasterPreset;
  const auto hd = wipreview::probe::resolveReviewRaster(
      ReviewRasterPreset::HD, 1, 1);
  const auto uhd = wipreview::probe::resolveReviewRaster(
      ReviewRasterPreset::UHD, 1, 1);
  const auto dci2k = wipreview::probe::resolveReviewRaster(
      ReviewRasterPreset::DCI2K, 1, 1);
  const auto dci4k = wipreview::probe::resolveReviewRaster(
      ReviewRasterPreset::DCI4K, 1, 1);
  const auto custom = wipreview::probe::resolveReviewRaster(
      ReviewRasterPreset::Custom, 3000, 2000);
  const auto clampedCustom = wipreview::probe::resolveReviewRaster(
      ReviewRasterPreset::Custom, 0, -1);
  assert(hd.width == 1920 && hd.height == 1080);
  assert(uhd.width == 3840 && uhd.height == 2160);
  assert(dci2k.width == 2048 && dci2k.height == 1080);
  assert(dci4k.width == 4096 && dci4k.height == 2160);
  assert(custom.width == 3000 && custom.height == 2000);
  assert(clampedCustom.width == 1 && clampedCustom.height == 1);
}

void testG02DciUsesHeightForTypeAndWidthForPadding() {
  constexpr double normalizedFontSize = 0.028;
  const double uhdFontPixels = normalizedFontSize * 2160.0;
  const double dciFontPixels = normalizedFontSize * 2160.0;
  assert(uhdFontPixels == dciFontPixels);
  const auto uhdCell = wipreview::probe::computeTextCell(
      {0, 0, 3840, 2160}, TextAnchor::TopLeft, 0.015, 0.015, 0.010);
  const auto dciCell = wipreview::probe::computeTextCell(
      {0, 0, 4096, 2160}, TextAnchor::TopLeft, 0.015, 0.015, 0.010);
  assert(uhdCell.x1 == 58);
  assert(dciCell.x1 == 61);
  assert(dciCell.x2 > uhdCell.x2);
  assert(uhdCell.y1 == dciCell.y1 && uhdCell.y2 == dciCell.y2);
}

void testG03AnamorphicParPlacement() {
  RenderOptions options;
  options.placement = PlacementMode::Fit;
  options.sourcePixelAspect = 4.0 / 3.0;
  options.outputPixelAspect = 1.0;
  const auto transform = wipreview::probe::computePlacement(
      {0, 0, 1440, 1080}, {0, 0, 1920, 1080}, options);
  assert(std::abs(transform.scaleX - 4.0 / 3.0) < 1.0e-12);
  assert(std::abs(transform.scaleY - 1.0) < 1.0e-12);
}

void testC06ManagedIdentity() {
  constexpr int width = 3;
  constexpr int height = 2;
  const std::array<wipreview::color::DisplayEncoding, 3> encodings{
      wipreview::color::DisplayEncoding::Rec709Gamma24,
      wipreview::color::DisplayEncoding::Rec2100PQ,
      wipreview::color::DisplayEncoding::Rec2100HLG};
  for (const auto encoding : encodings) {
    std::vector<float> source{
        0.05F, 0.10F, 0.20F, 1.0F, 0.25F, 0.50F, 0.75F, 1.0F,
        0.90F, 0.40F, 0.15F, 1.0F, 0.12F, 0.34F, 0.56F, 1.0F,
        0.66F, 0.33F, 0.11F, 1.0F, 1.00F, 0.80F, 0.60F, 1.0F};
    std::vector<float> working(source.size());
    std::vector<float> output(source.size());
    auto sourceView = floatView(source, width, height);
    auto workingView = floatView(working, width, height);
    auto outputView = floatView(output, width, height);
    RenderOptions options;
    options.placement = PlacementMode::Identity;
    wipreview::color::DisplayConfig color;
    color.encoding = encoding;
    assert(wipreview::probe::renderManagedDisplayFrame(
        sourceView, workingView, workingView.bounds, options, color));
    wipreview::probe::encodeManagedDisplayFrame(
        workingView, outputView, outputView.bounds, color, true);
    for (std::size_t index = 0; index < source.size(); ++index) {
      assert(std::abs(source[index] - output[index]) < 2.0e-5F);
    }
  }
}

void testPixelDepthRoundTrips() {
  std::array<std::uint8_t, 4> byteSource{64, 128, 192, 255};
  std::array<std::uint16_t, 4> shortOutput{};
  std::array<std::uint16_t, 4> halfOutput{};
  std::array<float, 4> floatOutput{};
  const ImageView byteView{reinterpret_cast<std::byte*>(byteSource.data()),
                           {0, 0, 1, 1}, 4, 4, 4, ChannelType::UInt8};
  const ImageView shortView{reinterpret_cast<std::byte*>(shortOutput.data()),
                            {0, 0, 1, 1}, 8, 8, 4, ChannelType::UInt16};
  const ImageView halfView{reinterpret_cast<std::byte*>(halfOutput.data()),
                           {0, 0, 1, 1}, 8, 8, 4, ChannelType::Half};
  const ImageView floatOutputView{
      reinterpret_cast<std::byte*>(floatOutput.data()), {0, 0, 1, 1}, 16,
      16, 4, ChannelType::Float32};
  RenderOptions identity;
  identity.placement = PlacementMode::Identity;
  wipreview::probe::renderStaticFrame(
      byteView, shortView, shortView.bounds, identity);
  assert(std::abs(static_cast<int>(shortOutput[0]) - 16448) <= 1);
  assert(std::abs(static_cast<int>(shortOutput[1]) - 32896) <= 1);
  assert(std::abs(static_cast<int>(shortOutput[2]) - 49344) <= 1);
  assert(shortOutput[3] == 65535);
  wipreview::probe::renderStaticFrame(
      byteView, halfView, halfView.bounds, identity);
  wipreview::probe::renderStaticFrame(
      halfView, floatOutputView, floatOutputView.bounds, identity);
  assert(std::abs(floatOutput[0] - 64.0F / 255.0F) < 3.0e-4F);
  assert(std::abs(floatOutput[1] - 128.0F / 255.0F) < 3.0e-4F);
  assert(std::abs(floatOutput[2] - 192.0F / 255.0F) < 3.0e-4F);
  assert(floatOutput[3] == 1.0F);
}

}  // namespace

int main() {
  testR03IntegratedAndUpstreamEquivalence();
  testG01HdUhdNormalizedLayout();
  testReviewRasterPresets();
  testG02DciUsesHeightForTypeAndWidthForPadding();
  testG03AnamorphicParPlacement();
  testC06ManagedIdentity();
  testPixelDepthRoundTrips();
  return 0;
}
