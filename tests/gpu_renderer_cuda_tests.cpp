#include "gpu_renderer.hpp"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using wipreview::probe::ChannelType;
using wipreview::probe::ImageView;

ImageView floatView(std::vector<float>& pixels, int width, int height) {
  return {reinterpret_cast<std::byte*>(pixels.data()), {0, 0, width, height},
          static_cast<std::ptrdiff_t>(width * 4 * sizeof(float)),
          4 * sizeof(float), 4, ChannelType::Float32};
}

} // namespace

int main() {
  int devices = 0;
  if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
    std::cout << "CUDA renderer tests skipped: no GPU device\n";
    return 0;
  }
  constexpr int sourceWidth = 7, sourceHeight = 5, outputWidth = 11,
                outputHeight = 8;
  std::vector<float> source(sourceWidth * sourceHeight * 4);
  std::vector<float> gpu(outputWidth * outputHeight * 4);
  std::vector<float> linear(gpu.size()), cpu(gpu.size());
  for (int y = 0; y < sourceHeight; ++y) for (int x = 0; x < sourceWidth; ++x) {
    const std::size_t offset = static_cast<std::size_t>((y * sourceWidth + x) * 4);
    source[offset] = .05F + .8F * x / std::max(1, sourceWidth - 1);
    source[offset + 1] = .1F + .7F * y / std::max(1, sourceHeight - 1);
    source[offset + 2] = .15F + .6F * ((x + y) % 5) / 4.F;
    source[offset + 3] = 1.F;
  }
  const auto sourceView = floatView(source, sourceWidth, sourceHeight);
  const auto gpuView = floatView(gpu, outputWidth, outputHeight);
  const auto linearView = floatView(linear, outputWidth, outputHeight);
  const auto cpuView = floatView(cpu, outputWidth, outputHeight);
  wipreview::probe::RenderOptions options;
  options.placement = wipreview::probe::PlacementMode::Fit;
  options.filter = wipreview::probe::ResampleFilter::Lanczos3;
  options.canvas[3] = 1.F;
  wipreview::color::DisplayConfig color;
  assert(wipreview::probe::renderManagedDisplayFrame(sourceView, linearView,
      linearView.bounds, options, color));
  wipreview::probe::encodeManagedDisplayFrame(linearView, cpuView,
      cpuView.bounds, color, true);
  float* deviceSource = nullptr;
  float* deviceOutput = nullptr;
  cudaStream_t stream = nullptr;
  assert(cudaStreamCreate(&stream) == cudaSuccess);
  assert(cudaMalloc(&deviceSource, source.size() * sizeof(float)) == cudaSuccess);
  assert(cudaMalloc(&deviceOutput, gpu.size() * sizeof(float)) == cudaSuccess);
  assert(cudaMemcpyAsync(deviceSource, source.data(), source.size() * sizeof(float),
      cudaMemcpyHostToDevice, stream) == cudaSuccess);
  wipreview::gpu::RenderRequest request;
  request.sourceBuffer = deviceSource;
  request.outputBuffer = deviceOutput;
  request.commandQueue = stream;
  request.sourceFormat = sourceView;
  request.outputFormat = gpuView;
  request.renderWindow = gpuView.bounds;
  request.renderOptions = options;
  request.color = color;
  std::string error;
  assert(wipreview::gpu::renderCUDA(request, error) ==
      wipreview::gpu::RenderStatus::Rendered);
  assert(cudaMemcpyAsync(gpu.data(), deviceOutput, gpu.size() * sizeof(float),
      cudaMemcpyDeviceToHost, stream) == cudaSuccess);
  assert(cudaStreamSynchronize(stream) == cudaSuccess);
  cudaFree(deviceOutput);
  cudaFree(deviceSource);
  cudaStreamDestroy(stream);
  for (std::size_t index = 0; index < cpu.size(); ++index) {
    assert(std::abs(cpu[index] - gpu[index]) < 1.5e-3F);
  }
  std::cout << "CUDA renderer tests passed\n";
}
