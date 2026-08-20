#include "gpu_renderer.hpp"

#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using wipreview::probe::ChannelType;
using wipreview::probe::GlyphMaskView;
using wipreview::probe::ImageView;
using wipreview::probe::PlacementMode;
using wipreview::probe::RectI;
using wipreview::probe::RenderOptions;
using wipreview::probe::ResampleFilter;
using wipreview::probe::TextAnchor;
using wipreview::probe::TextOverlayOptions;

ImageView floatView(std::vector<float> &pixels, int width, int height) {
  return {reinterpret_cast<std::byte *>(pixels.data()),
          {0, 0, width, height},
          static_cast<std::ptrdiff_t>(width * 4 * sizeof(float)),
          4 * sizeof(float),
          4,
          ChannelType::Float32};
}

std::vector<float> makeSource(int width, int height) {
  std::vector<float> pixels(static_cast<std::size_t>(width * height * 4));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t offset = static_cast<std::size_t>((y * width + x) * 4);
      const float alpha =
          0.35F + 0.65F * static_cast<float>((x + 2 * y) % 7) / 6.0F;
      pixels[offset + 0] =
          (0.05F + 0.85F * static_cast<float>(x) /
                       static_cast<float>(std::max(1, width - 1))) *
          alpha;
      pixels[offset + 1] =
          (0.10F + 0.75F * static_cast<float>(y) /
                       static_cast<float>(std::max(1, height - 1))) *
          alpha;
      pixels[offset + 2] =
          (0.15F + 0.60F * static_cast<float>((x + y) % 5) / 4.0F) * alpha;
      pixels[offset + 3] = alpha;
    }
  }
  return pixels;
}

void waitForQueue(id<MTLCommandQueue> queue) {
  id<MTLCommandBuffer> marker = [queue commandBuffer];
  [marker commit];
  [marker waitUntilCompleted];
  assert(marker.status == MTLCommandBufferStatusCompleted);
}

void compareCase(id<MTLDevice> device, id<MTLCommandQueue> queue,
                 int sourceWidth, int sourceHeight, int outputWidth,
                 int outputHeight, PlacementMode placement,
                 ResampleFilter filter,
                 wipreview::color::DisplayEncoding encoding, bool overlays) {
  auto source = makeSource(sourceWidth, sourceHeight);
  std::vector<float> gpuOutput(
      static_cast<std::size_t>(outputWidth * outputHeight * 4));
  std::vector<float> cpuLinear(gpuOutput.size());
  std::vector<float> cpuOutput(gpuOutput.size());
  auto sourceView = floatView(source, sourceWidth, sourceHeight);
  auto gpuOutputView = floatView(gpuOutput, outputWidth, outputHeight);
  auto cpuLinearView = floatView(cpuLinear, outputWidth, outputHeight);
  auto cpuOutputView = floatView(cpuOutput, outputWidth, outputHeight);

  RenderOptions options;
  options.placement = placement;
  options.filter = filter;
  options.sourcePremultiplied = true;
  options.outputPremultiplied = true;
  options.canvas[0] = 0.03F;
  options.canvas[1] = 0.06F;
  options.canvas[2] = 0.09F;
  options.canvas[3] = 0.8F;
  wipreview::color::DisplayConfig color;
  color.encoding = encoding;
  color.graphicsWhiteNits = 203.0;
  color.peakNits = 1000.0;

  wipreview::probe::BlankingOptions blanking;
  blanking.enabled = overlays;
  blanking.editorialAspect = 2.0;
  blanking.opacity = 0.45F;
  blanking.colour[0] = 0.12F;
  blanking.colour[1] = 0.04F;
  blanking.colour[2] = 0.20F;
  blanking.colour[3] = 0.75F;

  const std::array<std::uint8_t, 12> mask{0,   128, 255, 0,   255, 255,
                                          128, 0,   0,   128, 255, 0};
  const GlyphMaskView maskView{mask.data(), 4, 3, 4};
  TextOverlayOptions text;
  text.enabled = overlays;
  text.anchor = TextAnchor::TopLeft;
  text.colour[0] = 0.2F;
  text.colour[1] = 0.8F;
  text.colour[2] = 0.4F;
  text.colour[3] = 0.9F;
  text.opacity = 0.75F;

  assert(wipreview::probe::renderManagedDisplayFrame(
      sourceView, cpuLinearView, cpuLinearView.bounds, options, color));
  wipreview::probe::applyBlanking(cpuLinearView, cpuLinearView.bounds,
                                  blanking);
  if (overlays) {
    wipreview::probe::compositeTextMask(cpuLinearView, cpuLinearView.bounds,
                                        maskView, text);
  }
  wipreview::probe::encodeManagedDisplayFrame(
      cpuLinearView, cpuOutputView, cpuOutputView.bounds, color, true);

  id<MTLBuffer> sourceBuffer =
      [device newBufferWithBytes:source.data()
                          length:source.size() * sizeof(float)
                         options:MTLResourceStorageModeShared];
  id<MTLBuffer> outputBuffer =
      [device newBufferWithLength:gpuOutput.size() * sizeof(float)
                          options:MTLResourceStorageModeShared];
  assert(sourceBuffer && outputBuffer);

  wipreview::gpu::RenderRequest request;
  request.sourceBuffer = (__bridge void *)sourceBuffer;
  request.outputBuffer = (__bridge void *)outputBuffer;
  request.commandQueue = (__bridge void *)queue;
  request.sourceFormat = sourceView;
  request.outputFormat = gpuOutputView;
  request.renderWindow = gpuOutputView.bounds;
  request.sourcePremultiplied = true;
  request.outputPremultiplied = true;
  request.renderOptions = options;
  request.blanking = blanking;
  request.color = color;
  if (overlays) {
    wipreview::gpu::MaskLayer layer;
    layer.mask = maskView;
    layer.origin = wipreview::probe::computeTextOrigin(
        gpuOutputView.bounds, maskView.width, maskView.height, text);
    layer.cellBounds = text.cellBounds;
    layer.constrainToCell = text.constrainToCell;
    layer.colour[0] = text.colour[0];
    layer.colour[1] = text.colour[1];
    layer.colour[2] = text.colour[2];
    layer.colour[3] = text.colour[3];
    layer.opacity = text.opacity;
    request.layers.push_back(layer);
  }
  std::string error;
  assert(wipreview::gpu::renderMetal(request, error) ==
         wipreview::gpu::RenderStatus::Rendered);
  waitForQueue(queue);
  std::copy_n(static_cast<const float *>(outputBuffer.contents),
              gpuOutput.size(), gpuOutput.begin());

  const float tolerance =
      encoding == wipreview::color::DisplayEncoding::Rec709Gamma24 ? 3.0e-4F
                                                                   : 1.5e-3F;
  for (std::size_t index = 0; index < cpuOutput.size(); ++index) {
    if (std::abs(cpuOutput[index] - gpuOutput[index]) > tolerance) {
      std::cerr << "GPU mismatch index=" << index << " cpu=" << cpuOutput[index]
                << " gpu=" << gpuOutput[index] << " tolerance=" << tolerance
                << '\n';
      assert(false);
    }
  }
}

} // namespace

int main() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    assert(device);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    assert(queue);
    const std::array<PlacementMode, 5> placements{
        PlacementMode::Identity, PlacementMode::Fit, PlacementMode::Fill,
        PlacementMode::Stretch, PlacementMode::OneToOne};
    const std::array<ResampleFilter, 3> filters{ResampleFilter::Bilinear,
                                                ResampleFilter::Bicubic,
                                                ResampleFilter::Lanczos3};
    for (const auto placement : placements) {
      for (const auto filter : filters) {
        compareCase(device, queue, 7, 5, 11, 8, placement, filter,
                    wipreview::color::DisplayEncoding::Rec709Gamma24, false);
      }
    }
    for (const auto encoding :
         {wipreview::color::DisplayEncoding::Rec709Gamma24,
          wipreview::color::DisplayEncoding::Rec2100PQ,
          wipreview::color::DisplayEncoding::Rec2100HLG}) {
      compareCase(device, queue, 9, 7, 8, 10, PlacementMode::Fit,
                  ResampleFilter::Lanczos3, encoding, true);
    }
  }
  std::cout << "Metal renderer tests passed\n";
  return 0;
}
