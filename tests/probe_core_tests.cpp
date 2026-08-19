#include "probe_core.hpp"
#include "text_rasterizer.hpp"

#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>

using wipreview::probe::ImageView;
using wipreview::probe::ChannelType;
using wipreview::probe::BlankingOptions;
using wipreview::probe::PlacementMode;
using wipreview::probe::RectI;
using wipreview::probe::RenderOptions;
using wipreview::probe::TextAnchor;
using wipreview::probe::TextOverlayOptions;

namespace {

void testIntersection() {
  const RectI value = wipreview::probe::intersect({0, 0, 10, 10}, {3, -2, 12, 7});
  assert(value.x1 == 3 && value.y1 == 0 && value.x2 == 10 && value.y2 == 7);
  assert(wipreview::probe::empty({1, 1, 1, 4}));
}

void testCopyAndClear() {
  std::array<std::uint8_t, 16> source{};
  std::array<std::uint8_t, 36> destination{};
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = static_cast<std::uint8_t>(i + 1);
  }
  destination.fill(0xff);

  const ImageView src{reinterpret_cast<std::byte*>(source.data()), {1, 1, 5, 5}, 4, 1};
  const ImageView dst{reinterpret_cast<std::byte*>(destination.data()), {0, 0, 6, 6}, 6, 1};
  wipreview::probe::copyProbeFrame(src, dst, {0, 0, 6, 6});

  assert(destination[0] == 0);
  assert(destination[1 + 1 * 6] == 1);
  assert(destination[4 + 4 * 6] == 16);
  assert(destination[5 + 5 * 6] == 0);
}

void testNegativeRowBytes() {
  std::array<std::uint8_t, 4> source{1, 2, 3, 4};
  std::array<std::uint8_t, 4> destination{};

  const ImageView src{reinterpret_cast<std::byte*>(source.data() + 2), {0, 0, 2, 2}, -2, 1};
  const ImageView dst{reinterpret_cast<std::byte*>(destination.data() + 2), {0, 0, 2, 2}, -2, 1};
  wipreview::probe::copyProbeFrame(src, dst, {0, 0, 2, 2});
  assert(destination == source);
}

ImageView rgbaFloatView(float* data, RectI bounds, std::ptrdiff_t rowBytes) {
  return {reinterpret_cast<std::byte*>(data), bounds, rowBytes,
          sizeof(float) * 4, 4, ChannelType::Float32};
}

void setGray(std::array<float, 32>& pixels, int width, int x, int y, float value) {
  const std::size_t offset = static_cast<std::size_t>((y * width + x) * 4);
  pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = value;
  pixels[offset + 3] = 1.0F;
}

float red(const float* pixels, int width, int x, int y) {
  return pixels[(y * width + x) * 4];
}

float green(const float* pixels, int width, int x, int y) {
  return pixels[(y * width + x) * 4 + 1];
}

void testPlacementTransformsAndPAR() {
  RenderOptions options;
  options.placement = PlacementMode::Fit;
  auto transform = wipreview::probe::computePlacement({0, 0, 4, 2}, {0, 0, 4, 4}, options);
  assert(transform.scaleX == 1.0 && transform.scaleY == 1.0);

  options.placement = PlacementMode::Fill;
  transform = wipreview::probe::computePlacement({0, 0, 4, 2}, {0, 0, 2, 2}, options);
  assert(transform.scaleX == 1.0 && transform.scaleY == 1.0);

  options.placement = PlacementMode::Fit;
  options.sourcePixelAspect = 2.0;
  options.outputPixelAspect = 1.0;
  transform = wipreview::probe::computePlacement({0, 0, 2, 2}, {0, 0, 4, 4}, options);
  assert(transform.scaleX == 2.0 && transform.scaleY == 1.0);

  options.placement = PlacementMode::Stretch;
  transform = wipreview::probe::computePlacement({0, 0, 2, 4}, {0, 0, 8, 8}, options);
  assert(transform.scaleX == 4.0 && transform.scaleY == 2.0);
}

void testFitCanvasAndRenderWindow() {
  std::array<float, 32> source{};       // 4x2 RGBA
  std::array<float, 64> destination{};  // 4x4 RGBA
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 4; ++x) setGray(source, 4, x, y, static_cast<float>(x + 1));
  destination.fill(-1.0F);

  RenderOptions options;
  options.placement = PlacementMode::Fit;
  options.filter = wipreview::probe::ResampleFilter::Bilinear;
  options.canvas[0] = 0.25F;
  const ImageView src = rgbaFloatView(source.data(), {0, 0, 4, 2}, 4 * 4 * sizeof(float));
  const ImageView dst = rgbaFloatView(destination.data(), {0, 0, 4, 4}, 4 * 4 * sizeof(float));
  wipreview::probe::renderStaticFrame(src, dst, {1, 0, 4, 4}, options);

  assert(red(destination.data(), 4, 0, 0) == -1.0F);  // outside renderWindow
  assert(std::abs(red(destination.data(), 4, 1, 0) - 0.25F) < 1.0e-6F);
  assert(std::abs(red(destination.data(), 4, 1, 1) - 2.0F) < 1.0e-6F);
  assert(std::abs(red(destination.data(), 4, 3, 2) - 4.0F) < 1.0e-6F);
  assert(std::abs(red(destination.data(), 4, 2, 3) - 0.25F) < 1.0e-6F);
}

void testFillAndOneToOne() {
  std::array<float, 32> source{};       // 4x2 RGBA
  std::array<float, 64> destination{};  // 4x4 RGBA
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 4; ++x) setGray(source, 4, x, y, static_cast<float>(x + 1));

  RenderOptions options;
  options.placement = PlacementMode::Fill;
  options.filter = wipreview::probe::ResampleFilter::Bilinear;
  const ImageView src = rgbaFloatView(source.data(), {0, 0, 4, 2}, 4 * 4 * sizeof(float));
  ImageView dst = rgbaFloatView(destination.data(), {0, 0, 2, 2}, 2 * 4 * sizeof(float));
  wipreview::probe::renderStaticFrame(src, dst, {0, 0, 2, 2}, options);
  assert(std::abs(red(destination.data(), 2, 0, 0) - 2.0F) < 1.0e-6F);
  assert(std::abs(red(destination.data(), 2, 1, 0) - 3.0F) < 1.0e-6F);

  destination.fill(-1.0F);
  options.placement = PlacementMode::OneToOne;
  dst = rgbaFloatView(destination.data(), {0, 0, 4, 4}, 4 * 4 * sizeof(float));
  wipreview::probe::renderStaticFrame(src, dst, {0, 0, 4, 4}, options);
  assert(red(destination.data(), 4, 0, 0) == 0.0F);
  assert(std::abs(red(destination.data(), 4, 1, 1) - 2.0F) < 1.0e-6F);
  assert(std::abs(red(destination.data(), 4, 3, 2) - 4.0F) < 1.0e-6F);
  assert(red(destination.data(), 4, 3, 3) == 0.0F);
}

void testIdentityAndUInt16Canvas() {
  std::array<std::uint16_t, 4> destination{};
  ImageView dst{reinterpret_cast<std::byte*>(destination.data()), {0, 0, 1, 1},
                static_cast<std::ptrdiff_t>(sizeof(destination)), sizeof(destination),
                4, ChannelType::UInt16};
  RenderOptions options;
  options.placement = PlacementMode::Identity;
  options.canvas[0] = 0.5F;
  options.canvas[1] = 0.25F;
  options.canvas[2] = 0.0F;
  options.canvas[3] = 1.0F;
  wipreview::probe::renderStaticFrame({}, dst, {0, 0, 1, 1}, options);
  assert(destination[0] == 32768);
  assert(destination[1] == 16384);
  assert(destination[2] == 0);
  assert(destination[3] == 65535);
}

void testPremultiplication() {
  std::array<float, 8> source{1.0F, 0.0F, 0.0F, 0.0F,
                              0.0F, 0.0F, 0.0F, 1.0F};
  std::array<float, 4> destination{};
  const ImageView src = rgbaFloatView(source.data(), {0, 0, 2, 1}, 8 * sizeof(float));
  const ImageView dst = rgbaFloatView(destination.data(), {0, 0, 1, 1}, 4 * sizeof(float));
  RenderOptions options;
  options.placement = PlacementMode::Stretch;
  options.filter = wipreview::probe::ResampleFilter::Bilinear;
  options.sourcePremultiplied = false;
  options.outputPremultiplied = false;
  wipreview::probe::renderStaticFrame(src, dst, {0, 0, 1, 1}, options);
  assert(std::abs(destination[0]) < 1.0e-6F);
  assert(std::abs(destination[3] - 0.5F) < 1.0e-6F);

  options.outputPremultiplied = true;
  options.canvas[0] = 1.0F;
  options.canvas[3] = 0.25F;
  wipreview::probe::renderStaticFrame({}, dst, {0, 0, 1, 1}, options);
  assert(std::abs(destination[0] - 0.25F) < 1.0e-6F);
  assert(std::abs(destination[3] - 0.25F) < 1.0e-6F);
}

void testDownsampleAntialias() {
  std::array<float, 16> source{};
  for (int x = 0; x < 4; ++x) {
    source[static_cast<std::size_t>(x * 4)] = x % 2 == 0 ? 0.0F : 1.0F;
    source[static_cast<std::size_t>(x * 4 + 3)] = 1.0F;
  }
  std::array<float, 4> destination{};
  const ImageView src = rgbaFloatView(source.data(), {0, 0, 4, 1}, 16 * sizeof(float));
  const ImageView dst = rgbaFloatView(destination.data(), {0, 0, 1, 1}, 4 * sizeof(float));
  RenderOptions options;
  options.placement = PlacementMode::Stretch;
  options.filter = wipreview::probe::ResampleFilter::Bilinear;
  wipreview::probe::renderStaticFrame(src, dst, {0, 0, 1, 1}, options);
  assert(std::abs(destination[0] - 0.5F) < 1.0e-6F);
}

template <std::size_t N>
void fillOpaqueWhite(std::array<float, N>& pixels) {
  static_assert(N % 4 == 0);
  for (std::size_t offset = 0; offset < N; offset += 4) {
    pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = pixels[offset + 3] = 1.0F;
  }
}

void testBlankingLetterboxAndOpacity() {
  std::array<float, 16 * 10 * 4> pixels{};
  fillOpaqueWhite(pixels);
  const ImageView dst = rgbaFloatView(pixels.data(), {0, 0, 16, 10}, 16 * 4 * sizeof(float));
  BlankingOptions blanking;
  blanking.enabled = true;
  blanking.editorialAspect = 2.0;
  wipreview::probe::applyBlanking(dst, {0, 0, 16, 10}, blanking);
  assert(red(pixels.data(), 16, 0, 0) == 0.0F);
  assert(red(pixels.data(), 16, 0, 1) == 1.0F);
  assert(red(pixels.data(), 16, 15, 9) == 0.0F);

  fillOpaqueWhite(pixels);
  blanking.opacity = 0.5F;
  wipreview::probe::applyBlanking(dst, {0, 0, 16, 10}, blanking);
  assert(std::abs(red(pixels.data(), 16, 0, 0) - 0.5F) < 1.0e-6F);
  assert(red(pixels.data(), 16, 0, 1) == 1.0F);

  fillOpaqueWhite(pixels);
  blanking.enabled = false;
  wipreview::probe::applyBlanking(dst, {0, 0, 16, 10}, blanking);
  assert(red(pixels.data(), 16, 0, 0) == 1.0F);
}

void testBlankingPillarboxPARAndFractionalEdge() {
  std::array<float, 20 * 10 * 4> pixels{};
  fillOpaqueWhite(pixels);
  const ImageView dst = rgbaFloatView(pixels.data(), {0, 0, 20, 10}, 20 * 4 * sizeof(float));
  BlankingOptions blanking;
  blanking.enabled = true;
  blanking.editorialAspect = 2.0;
  blanking.outputPixelAspect = 2.0;
  const auto aperture = wipreview::probe::computeBlankingAperture(dst.bounds, blanking);
  assert(aperture.x1 == 5.0 && aperture.x2 == 15.0);
  wipreview::probe::applyBlanking(dst, {0, 0, 20, 10}, blanking);
  assert(red(pixels.data(), 20, 4, 5) == 0.0F);
  assert(red(pixels.data(), 20, 5, 5) == 1.0F);
  assert(red(pixels.data(), 20, 15, 5) == 0.0F);

  std::array<float, 10 * 10 * 4> fractional{};
  fillOpaqueWhite(fractional);
  const ImageView fractionalDst = rgbaFloatView(
      fractional.data(), {0, 0, 10, 10}, 10 * 4 * sizeof(float));
  blanking.outputPixelAspect = 1.0;
  blanking.editorialAspect = 2.0;
  wipreview::probe::applyBlanking(fractionalDst, {0, 0, 10, 10}, blanking);
  assert(red(fractional.data(), 10, 0, 1) == 0.0F);
  assert(std::abs(red(fractional.data(), 10, 0, 2) - 0.5F) < 1.0e-6F);
  assert(red(fractional.data(), 10, 0, 3) == 1.0F);
}

void testBlankingStraightAlphaAndRenderWindow() {
  std::array<float, 4 * 2 * 4> pixels{};
  for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
    pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 1.0F;
    pixels[offset + 3] = 0.5F;
  }
  const ImageView dst = rgbaFloatView(pixels.data(), {0, 0, 4, 2}, 4 * 4 * sizeof(float));
  BlankingOptions blanking;
  blanking.enabled = true;
  blanking.editorialAspect = 1.0;
  blanking.opacity = 0.5F;
  blanking.outputPremultiplied = false;
  wipreview::probe::applyBlanking(dst, {0, 0, 1, 1}, blanking);
  assert(std::abs(red(pixels.data(), 4, 0, 0) - (1.0F / 3.0F)) < 1.0e-6F);
  assert(std::abs(pixels[3] - 0.75F) < 1.0e-6F);
  assert(red(pixels.data(), 4, 0, 1) == 1.0F);  // outside renderWindow
  assert(red(pixels.data(), 4, 3, 0) == 1.0F);  // outside renderWindow
}

void testTextAnchorsAndGrowth() {
  TextOverlayOptions options;
  options.paddingLeft = 0.10;
  options.paddingRight = 0.20;
  options.paddingTop = 0.10;
  options.paddingBottom = 0.05;

  options.anchor = TextAnchor::TopLeft;
  auto origin = wipreview::probe::computeTextOrigin({0, 0, 100, 100}, 20, 10, options);
  assert(origin.x == 10 && origin.y == 80);
  const auto taller = wipreview::probe::computeTextOrigin({0, 0, 100, 100}, 20, 20, options);
  assert(taller.x == 10 && taller.y == 70);
  assert(origin.y + 10 == taller.y + 20);  // fixed visible top edge

  options.anchor = TextAnchor::TopCenter;
  origin = wipreview::probe::computeTextOrigin({0, 0, 100, 100}, 20, 10, options);
  assert(origin.x == 40 && origin.y == 80);

  options.anchor = TextAnchor::TopRight;
  origin = wipreview::probe::computeTextOrigin({0, 0, 100, 100}, 20, 10, options);
  assert(origin.x == 60 && origin.y == 80);

  options.anchor = TextAnchor::BottomLeft;
  origin = wipreview::probe::computeTextOrigin({0, 0, 100, 100}, 20, 10, options);
  assert(origin.x == 10 && origin.y == 5);
  const auto bottomTaller = wipreview::probe::computeTextOrigin(
      {0, 0, 100, 100}, 20, 20, options);
  assert(bottomTaller.x == 10 && bottomTaller.y == 5);  // grows upward

  options.offsetX = 0.25;
  options.offsetY = -0.10;
  origin = wipreview::probe::computeTextOrigin({0, 0, 100, 100}, 20, 10, options);
  assert(origin.x == 35 && origin.y == -5);

  options.anchor = TextAnchor::TopRight;
  options.offsetX = -0.05;
  options.offsetY = 0.05;
  origin = wipreview::probe::computeTextOrigin({10, 20, 210, 120}, 20, 10, options);
  assert(origin.x == 140 && origin.y == 105);
}

void testTextMaskComposition() {
  std::array<float, 4 * 4 * 4> pixels{};
  fillOpaqueWhite(pixels);
  const ImageView dst = rgbaFloatView(pixels.data(), {0, 0, 4, 4}, 4 * 4 * sizeof(float));
  const std::array<std::uint8_t, 4> mask{255, 128, 0, 255};
  const wipreview::probe::GlyphMaskView maskView{mask.data(), 2, 2, 2};
  TextOverlayOptions options;
  options.enabled = true;
  options.anchor = TextAnchor::BottomLeft;
  options.paddingLeft = options.paddingBottom = 0.0;
  options.colour[0] = options.colour[1] = options.colour[2] = 0.0F;
  wipreview::probe::compositeTextMask(dst, {0, 0, 4, 4}, maskView, options);
  assert(red(pixels.data(), 4, 0, 0) == 0.0F);
  assert(std::abs(red(pixels.data(), 4, 1, 0) - (127.0F / 255.0F)) < 1.0e-6F);
  assert(red(pixels.data(), 4, 0, 1) == 1.0F);
  assert(red(pixels.data(), 4, 1, 1) == 0.0F);
  assert(red(pixels.data(), 4, 3, 3) == 1.0F);
}

void testSystemTextRasterizerUTF8() {
  const auto regular = wipreview::text::rasterizeUTF8(
      "SECUENCIA ÁRTICO — VERSIÓN 03", "System Default",
      wipreview::text::FontStyle::Regular, 32.0);
  assert(!regular.fillPixels.empty());
  assert(regular.width > 0 && regular.height > 0);
  assert(!regular.resolvedFont.empty());

  const auto larger = wipreview::text::rasterizeUTF8(
      "ÁRTICO", "System Default", wipreview::text::FontStyle::Bold, 64.0);
  assert(!larger.fillPixels.empty());
  assert(larger.height > regular.height);

  const auto orientation = wipreview::text::rasterizeUTF8(
      "F", "System Default", wipreview::text::FontStyle::Regular, 96.0);
  assert(!orientation.fillPixels.empty() && orientation.height >= 3);
  std::uint64_t bottomInk = 0;
  std::uint64_t topInk = 0;
  for (int y = 0; y < orientation.height / 3; ++y) {
    for (int x = 0; x < orientation.width; ++x) {
      bottomInk += orientation.fillPixels[static_cast<std::size_t>(
          y * orientation.width + x)];
      topInk += orientation.fillPixels[static_cast<std::size_t>(
          (orientation.height - 1 - y) * orientation.width + x)];
    }
  }
  assert(topInk > bottomInk);  // Mask rows use OFX's bottom-up coordinates.

  const auto fallback = wipreview::text::rasterizeUTF8(
      "FALLBACK", "WIPReview Font That Does Not Exist 7F3A",
      wipreview::text::FontStyle::Regular, 32.0);
  assert(!fallback.fillPixels.empty());
  assert(fallback.usedFallback);
  assert(!fallback.resolvedFont.empty());

  const std::string invalidUTF8{"\xff\xfe", 2};
  const auto invalid = wipreview::text::rasterizeUTF8(
      invalidUTF8, "System Default", wipreview::text::FontStyle::Regular, 32.0);
  assert(invalid.fillPixels.empty());
}

void testOutlineUsesGlyphAlpha() {
  wipreview::text::GlyphRaster glyph;
  glyph.width = 1;
  glyph.height = 1;
  glyph.fillPixels = {255};
  glyph.resolvedFont = "TestFont";

  assert(wipreview::text::addOutline(glyph, 1));
  assert(glyph.width == 3 && glyph.height == 3);
  const std::array<std::uint8_t, 9> expectedFill{
      0, 0, 0,
      0, 255, 0,
      0, 0, 0};
  const std::array<std::uint8_t, 9> expectedOutline{
      0, 255, 0,
      255, 255, 255,
      0, 255, 0};
  assert(std::equal(glyph.fillPixels.begin(), glyph.fillPixels.end(),
                    expectedFill.begin(), expectedFill.end()));
  assert(std::equal(glyph.outlinePixels.begin(), glyph.outlinePixels.end(),
                    expectedOutline.begin(), expectedOutline.end()));
  assert(glyph.resolvedFont == "TestFont");

  std::array<float, 5 * 5 * 4> pixels{};
  fillOpaqueWhite(pixels);
  const ImageView dst = rgbaFloatView(
      pixels.data(), {0, 0, 5, 5}, 5 * 4 * sizeof(float));
  TextOverlayOptions outline;
  outline.enabled = true;
  outline.anchor = TextAnchor::BottomLeft;
  outline.paddingLeft = outline.paddingBottom = 0.0;
  outline.colour[0] = outline.colour[1] = outline.colour[2] = 0.0F;
  wipreview::probe::compositeTextMask(
      dst, {0, 0, 5, 5}, glyph.outlineView(), outline);

  TextOverlayOptions fill = outline;
  fill.colour[0] = 1.0F;
  wipreview::probe::compositeTextMask(
      dst, {0, 0, 5, 5}, glyph.fillView(), fill);
  assert(red(pixels.data(), 5, 1, 0) == 0.0F);    // outline-only pixel
  assert(red(pixels.data(), 5, 1, 1) == 1.0F);   // red fill over outline
  assert(green(pixels.data(), 5, 1, 1) == 0.0F);
  assert(red(pixels.data(), 5, 0, 0) == 1.0F);    // outside both masks
}

void testShadowUsesGlyphAlpha() {
  wipreview::text::GlyphRaster offsetGlyph;
  offsetGlyph.width = 1;
  offsetGlyph.height = 1;
  offsetGlyph.fillPixels = {255};
  assert(wipreview::text::addShadow(offsetGlyph, 1, 1, 0.0));
  assert(offsetGlyph.width == 2 && offsetGlyph.height == 2);
  const std::array<std::uint8_t, 4> expectedFill{0, 0, 255, 0};
  const std::array<std::uint8_t, 4> expectedShadow{0, 255, 0, 0};
  assert(std::equal(offsetGlyph.fillPixels.begin(), offsetGlyph.fillPixels.end(),
                    expectedFill.begin(), expectedFill.end()));
  assert(std::equal(offsetGlyph.shadowPixels.begin(), offsetGlyph.shadowPixels.end(),
                    expectedShadow.begin(), expectedShadow.end()));

  wipreview::text::GlyphRaster softGlyph;
  softGlyph.width = 1;
  softGlyph.height = 1;
  softGlyph.fillPixels = {255};
  assert(wipreview::text::addShadow(softGlyph, 0, 0, 1.0));
  assert(softGlyph.width == 7 && softGlyph.height == 7);
  const auto centre = softGlyph.shadowPixels[3 * 7 + 3];
  const auto neighbour = softGlyph.shadowPixels[3 * 7 + 4];
  assert(centre > neighbour && neighbour > 0 && centre < 255);
  std::uint64_t shadowSum = 0;
  for (const auto value : softGlyph.shadowPixels) shadowSum += value;
  assert(shadowSum > 240 && shadowSum < 270);

  wipreview::text::GlyphRaster styledGlyph;
  styledGlyph.width = 1;
  styledGlyph.height = 1;
  styledGlyph.fillPixels = {255};
  assert(wipreview::text::addOutline(styledGlyph, 1));
  assert(wipreview::text::addShadow(styledGlyph, 1, 1, 0.0));
  assert(styledGlyph.width == 4 && styledGlyph.height == 4);
  assert(styledGlyph.fillPixels.size() == 16);
  assert(styledGlyph.outlinePixels.size() == 16);
  assert(styledGlyph.shadowPixels.size() == 16);
}

}  // namespace

int main() {
  testIntersection();
  testCopyAndClear();
  testNegativeRowBytes();
  testPlacementTransformsAndPAR();
  testFitCanvasAndRenderWindow();
  testFillAndOneToOne();
  testIdentityAndUInt16Canvas();
  testPremultiplication();
  testDownsampleAntialias();
  testBlankingLetterboxAndOpacity();
  testBlankingPillarboxPARAndFractionalEdge();
  testBlankingStraightAlphaAndRenderWindow();
  testTextAnchorsAndGrowth();
  testTextMaskComposition();
  testSystemTextRasterizerUTF8();
  testOutlineUsesGlyphAlpha();
  testShadowUsesGlyphAlpha();
  return 0;
}
