#include "gpu_renderer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <CL/cl.h>

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
using wipreview::probe::ImageView;
using wipreview::probe::PlacementMode;
using wipreview::probe::RenderOptions;
using wipreview::probe::ResampleFilter;

class OpenCL final {
 public:
  OpenCL() : module_(LoadLibraryW(L"OpenCL.dll")) {
    if (!module_) return;
    getPlatformIDs_ = load<decltype(getPlatformIDs_)>("clGetPlatformIDs");
    getDeviceIDs_ = load<decltype(getDeviceIDs_)>("clGetDeviceIDs");
    createContext_ = load<decltype(createContext_)>("clCreateContext");
    createCommandQueue_ =
        load<decltype(createCommandQueue_)>("clCreateCommandQueue");
    createBuffer_ = load<decltype(createBuffer_)>("clCreateBuffer");
    enqueueReadBuffer_ =
        load<decltype(enqueueReadBuffer_)>("clEnqueueReadBuffer");
    finish_ = load<decltype(finish_)>("clFinish");
    releaseMem_ = load<decltype(releaseMem_)>("clReleaseMemObject");
    releaseQueue_ = load<decltype(releaseQueue_)>("clReleaseCommandQueue");
    releaseContext_ = load<decltype(releaseContext_)>("clReleaseContext");
  }

  ~OpenCL() {
    if (module_) FreeLibrary(module_);
  }

  bool ready() const {
    return module_ && getPlatformIDs_ && getDeviceIDs_ && createContext_ &&
        createCommandQueue_ && createBuffer_ && enqueueReadBuffer_ && finish_ &&
        releaseMem_ && releaseQueue_ && releaseContext_;
  }

  decltype(&::clGetPlatformIDs) getPlatformIDs_ = nullptr;
  decltype(&::clGetDeviceIDs) getDeviceIDs_ = nullptr;
  decltype(&::clCreateContext) createContext_ = nullptr;
  decltype(&::clCreateCommandQueue) createCommandQueue_ = nullptr;
  decltype(&::clCreateBuffer) createBuffer_ = nullptr;
  decltype(&::clEnqueueReadBuffer) enqueueReadBuffer_ = nullptr;
  decltype(&::clFinish) finish_ = nullptr;
  decltype(&::clReleaseMemObject) releaseMem_ = nullptr;
  decltype(&::clReleaseCommandQueue) releaseQueue_ = nullptr;
  decltype(&::clReleaseContext) releaseContext_ = nullptr;

 private:
  template <typename T>
  T load(const char* name) const {
    return reinterpret_cast<T>(GetProcAddress(module_, name));
  }

  HMODULE module_ = nullptr;
};

ImageView floatView(std::vector<float>& pixels, int width, int height) {
  return {reinterpret_cast<std::byte*>(pixels.data()), {0, 0, width, height},
          static_cast<std::ptrdiff_t>(width * 4 * sizeof(float)),
          4 * sizeof(float), 4, ChannelType::Float32};
}

std::vector<float> makeSource(int width, int height) {
  std::vector<float> pixels(static_cast<std::size_t>(width * height * 4));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto offset = static_cast<std::size_t>((y * width + x) * 4);
      pixels[offset] = 0.05F + 0.8F * x / std::max(1, width - 1);
      pixels[offset + 1] = 0.1F + 0.7F * y / std::max(1, height - 1);
      pixels[offset + 2] = 0.15F + 0.6F * ((x + y) % 5) / 4.0F;
      pixels[offset + 3] = 1.0F;
    }
  }
  return pixels;
}

void compareFit(OpenCL& opencl, cl_device_id device) {
  constexpr int sourceWidth = 7;
  constexpr int sourceHeight = 5;
  constexpr int outputWidth = 11;
  constexpr int outputHeight = 8;
  auto source = makeSource(sourceWidth, sourceHeight);
  std::vector<float> gpuOutput(outputWidth * outputHeight * 4);
  std::vector<float> cpuLinear(gpuOutput.size());
  std::vector<float> cpuOutput(gpuOutput.size());
  const auto sourceView = floatView(source, sourceWidth, sourceHeight);
  const auto gpuView = floatView(gpuOutput, outputWidth, outputHeight);
  const auto cpuLinearView = floatView(cpuLinear, outputWidth, outputHeight);
  const auto cpuOutputView = floatView(cpuOutput, outputWidth, outputHeight);

  RenderOptions options;
  options.placement = PlacementMode::Fit;
  options.filter = ResampleFilter::Lanczos3;
  options.sourcePremultiplied = true;
  options.outputPremultiplied = true;
  options.canvas[3] = 1.0F;
  wipreview::color::DisplayConfig color;
  assert(wipreview::probe::renderManagedDisplayFrame(
      sourceView, cpuLinearView, cpuLinearView.bounds, options, color));
  wipreview::probe::encodeManagedDisplayFrame(
      cpuLinearView, cpuOutputView, cpuOutputView.bounds, color, true);

  cl_int status = CL_SUCCESS;
  const cl_context context = opencl.createContext_(
      nullptr, 1, &device, nullptr, nullptr, &status);
  assert(status == CL_SUCCESS && context);
  const cl_command_queue queue =
      opencl.createCommandQueue_(context, device, 0, &status);
  assert(status == CL_SUCCESS && queue);
  const cl_mem sourceBuffer = opencl.createBuffer_(
      context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
      source.size() * sizeof(float), source.data(), &status);
  assert(status == CL_SUCCESS && sourceBuffer);
  const cl_mem outputBuffer = opencl.createBuffer_(
      context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
      gpuOutput.size() * sizeof(float), gpuOutput.data(), &status);
  assert(status == CL_SUCCESS && outputBuffer);

  wipreview::gpu::RenderRequest request;
  request.sourceBuffer = reinterpret_cast<void*>(sourceBuffer);
  request.outputBuffer = reinterpret_cast<void*>(outputBuffer);
  request.commandQueue = reinterpret_cast<void*>(queue);
  request.sourceFormat = sourceView;
  request.outputFormat = gpuView;
  request.renderWindow = gpuView.bounds;
  request.sourcePremultiplied = true;
  request.outputPremultiplied = true;
  request.renderOptions = options;
  request.color = color;
  std::string error;
  assert(wipreview::gpu::renderOpenCL(request, error) ==
         wipreview::gpu::RenderStatus::Rendered);
  assert(opencl.finish_(queue) == CL_SUCCESS);
  assert(opencl.enqueueReadBuffer_(queue, outputBuffer, CL_TRUE, 0,
                                   gpuOutput.size() * sizeof(float),
                                   gpuOutput.data(), 0, nullptr, nullptr) ==
         CL_SUCCESS);
  opencl.releaseMem_(outputBuffer);
  opencl.releaseMem_(sourceBuffer);
  opencl.releaseQueue_(queue);
  opencl.releaseContext_(context);

  for (std::size_t index = 0; index < cpuOutput.size(); ++index) {
    assert(std::abs(cpuOutput[index] - gpuOutput[index]) < 1.5e-3F);
  }
}

}  // namespace

int main() {
  OpenCL opencl;
  if (!opencl.ready()) {
    std::cout << "OpenCL renderer tests skipped: OpenCL runtime unavailable\n";
    return 0;
  }
  cl_uint platformCount = 0;
  if (opencl.getPlatformIDs_(0, nullptr, &platformCount) != CL_SUCCESS ||
      platformCount == 0) {
    std::cout << "OpenCL renderer tests skipped: no platform\n";
    return 0;
  }
  std::vector<cl_platform_id> platforms(platformCount);
  assert(opencl.getPlatformIDs_(platformCount, platforms.data(), nullptr) ==
         CL_SUCCESS);
  for (const auto platform : platforms) {
    cl_uint deviceCount = 0;
    if (opencl.getDeviceIDs_(platform, CL_DEVICE_TYPE_GPU, 0, nullptr,
                             &deviceCount) != CL_SUCCESS ||
        deviceCount == 0) {
      continue;
    }
    std::vector<cl_device_id> devices(deviceCount);
    assert(opencl.getDeviceIDs_(platform, CL_DEVICE_TYPE_GPU, deviceCount,
                                devices.data(), nullptr) == CL_SUCCESS);
    compareFit(opencl, devices.front());
    std::cout << "OpenCL renderer tests passed\n";
    return 0;
  }
  std::cout << "OpenCL renderer tests skipped: no GPU device\n";
  return 0;
}
