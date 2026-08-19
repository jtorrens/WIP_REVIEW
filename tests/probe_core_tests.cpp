#include "probe_core.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>

using wipreview::probe::ImageView;
using wipreview::probe::ChannelType;
using wipreview::probe::PlacementMode;
using wipreview::probe::RectI;
using wipreview::probe::RenderOptions;

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
  assert(std::abs(red(destination.data(), 4, 1, 1) - 1.0F) < 1.0e-6F);
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

}  // namespace

int main() {
  testIntersection();
  testCopyAndClear();
  testNegativeRowBytes();
  testPlacementTransformsAndPAR();
  testFitCanvasAndRenderWindow();
  testFillAndOneToOne();
  testIdentityAndUInt16Canvas();
  return 0;
}
