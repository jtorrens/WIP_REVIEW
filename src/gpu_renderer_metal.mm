#include "gpu_renderer.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <simd/simd.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

namespace wipreview::gpu {
namespace {

constexpr char kMetalSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct Params {
  int4 sourceBounds;
  int4 outputBounds;
  int4 renderWindow;
  int sourceRowBytes;
  int outputRowBytes;
  int sourcePixelBytes;
  int outputPixelBytes;
  int channelType;
  int sourcePremultiplied;
  int outputPremultiplied;
  int encoding;
  float graphicsWhite;
  float peakNits;
  int filter;
  float4 transform;
  float4 canvas;
  int blankingEnabled;
  float blankingOpacity;
  float2 reserved0;
  float4 blankingColour;
  float4 aperture;
  int layerCount;
  int3 reserved1;
};

struct Layer {
  int4 geometry;
  int4 cellBounds;
  int maskOffset;
  int constrainToCell;
  int2 reserved;
  float4 colour;
  float opacity;
  float3 reserved2;
};

float readChannel(device const uchar* pixel, int channel, int type) {
  if (type == 0) return float(pixel[channel]) / 255.0f;
  if (type == 1) return float(reinterpret_cast<device const ushort*>(pixel)[channel]) / 65535.0f;
  if (type == 2) return float(reinterpret_cast<device const half*>(pixel)[channel]);
  return reinterpret_cast<device const float*>(pixel)[channel];
}

void writeChannel(device uchar* pixel, int channel, int type, float value) {
  if (type == 0) {
    pixel[channel] = uchar(round(clamp(value, 0.0f, 1.0f) * 255.0f));
  } else if (type == 1) {
    reinterpret_cast<device ushort*>(pixel)[channel] =
        ushort(round(clamp(value, 0.0f, 1.0f) * 65535.0f));
  } else if (type == 2) {
    reinterpret_cast<device half*>(pixel)[channel] = half(value);
  } else {
    reinterpret_cast<device float*>(pixel)[channel] = value;
  }
}

float pqToNits(float encoded) {
  constexpr float m1 = 2610.0f / 16384.0f;
  constexpr float m2 = 2523.0f / 32.0f;
  constexpr float c1 = 3424.0f / 4096.0f;
  constexpr float c2 = 2413.0f / 128.0f;
  constexpr float c3 = 2392.0f / 128.0f;
  float value = pow(clamp(encoded, 0.0f, 1.0f), 1.0f / m2);
  float numerator = max(value - c1, 0.0f);
  float denominator = c2 - c3 * value;
  return denominator <= 0.0f ? 10000.0f :
      10000.0f * pow(numerator / denominator, 1.0f / m1);
}

float nitsToPq(float nits) {
  constexpr float m1 = 2610.0f / 16384.0f;
  constexpr float m2 = 2523.0f / 32.0f;
  constexpr float c1 = 3424.0f / 4096.0f;
  constexpr float c2 = 2413.0f / 128.0f;
  constexpr float c3 = 2392.0f / 128.0f;
  float luminance = pow(clamp(nits / 10000.0f, 0.0f, 1.0f), m1);
  return pow((c1 + c2 * luminance) / (1.0f + c3 * luminance), m2);
}

float hlgInverse(float encoded) {
  constexpr float a = 0.17883277f;
  constexpr float b = 0.28466892f;
  constexpr float c = 0.55991073f;
  float value = max(encoded, 0.0f);
  return value <= 0.5f ? value * value / 3.0f :
      (exp((value - c) / a) + b) / 12.0f;
}

float hlgForward(float linear) {
  constexpr float a = 0.17883277f;
  constexpr float b = 0.28466892f;
  constexpr float c = 0.55991073f;
  float value = max(linear, 0.0f);
  return value <= 1.0f / 12.0f ? sqrt(3.0f * value) :
      a * log(12.0f * value - b) + c;
}

float3 decodeDisplay(float3 encoded, constant Params& p) {
  if (p.encoding == 0) {
    return sign(encoded) * pow(abs(encoded), float3(2.4f));
  }
  if (p.encoding == 1) {
    return float3(pqToNits(encoded.r), pqToNits(encoded.g),
                  pqToNits(encoded.b)) / p.graphicsWhite;
  }
  float3 scene = float3(hlgInverse(encoded.r), hlgInverse(encoded.g),
                        hlgInverse(encoded.b));
  float luminance = max(dot(scene, float3(0.2627f, 0.6780f, 0.0593f)), 0.0f);
  float gamma = 1.2f + 0.42f * log10(p.peakNits / 1000.0f);
  float scale = p.peakNits *
      (luminance > 0.0f ? pow(luminance, gamma - 1.0f) : 0.0f);
  return scene * scale / p.graphicsWhite;
}

float3 encodeDisplay(float3 linear, constant Params& p) {
  if (p.encoding == 0) {
    return sign(linear) * pow(abs(linear), float3(1.0f / 2.4f));
  }
  if (p.encoding == 1) {
    float3 nits = linear * p.graphicsWhite;
    return float3(nitsToPq(nits.r), nitsToPq(nits.g), nitsToPq(nits.b));
  }
  float3 nits = max(linear * p.graphicsWhite, float3(0.0f));
  float luminance = max(dot(nits, float3(0.2627f, 0.6780f, 0.0593f)), 0.0f);
  float gamma = 1.2f + 0.42f * log10(p.peakNits / 1000.0f);
  float sceneLuminance = luminance > 0.0f ?
      pow(luminance / p.peakNits, 1.0f / gamma) : 0.0f;
  float scale = sceneLuminance > 0.0f ?
      p.peakNits * pow(sceneLuminance, gamma - 1.0f) : 1.0f;
  float3 scene = nits / scale;
  return float3(hlgForward(scene.r), hlgForward(scene.g),
                hlgForward(scene.b));
}

float blankingCoverage(int2 coordinate, constant Params& p) {
  bool horizontal = p.aperture.y > float(p.outputBounds.y) ||
      p.aperture.w < float(p.outputBounds.w);
  if (horizontal) {
    float overlap = max(0.0f, min(float(coordinate.y + 1), p.aperture.w) -
        max(float(coordinate.y), p.aperture.y));
    return 1.0f - overlap;
  }
  float overlap = max(0.0f, min(float(coordinate.x + 1), p.aperture.z) -
      max(float(coordinate.x), p.aperture.x));
  return 1.0f - overlap;
}

float4 over(float4 base, float3 colour, float alpha) {
  return float4(colour * alpha + base.rgb * (1.0f - alpha),
                alpha + base.a * (1.0f - alpha));
}

float filterKernel(float distance, int filter) {
  float x = abs(distance);
  if (filter == 0) return x < 1.0f ? 1.0f - x : 0.0f;
  if (filter == 1) {
    if (x < 1.0f) return 1.5f*x*x*x - 2.5f*x*x + 1.0f;
    if (x < 2.0f) return -0.5f*x*x*x + 2.5f*x*x - 4.0f*x + 2.0f;
    return 0.0f;
  }
  if (x >= 3.0f) return 0.0f;
  if (x < 1.0e-6f) return 1.0f;
  constexpr float pi = 3.14159265358979323846f;
  float pix = pi * x;
  return (sin(pix) / pix) * (sin(pix / 3.0f) / (pix / 3.0f));
}

float4 applyOverlays(float4 linear, int2 coordinate,
                     device const uchar* masks,
                     constant Params& p, constant Layer* layers) {
  if (p.blankingEnabled != 0) {
    float coverage = blankingCoverage(coordinate, p);
    float layerAlpha = clamp(coverage * p.blankingOpacity *
                             p.blankingColour.a, 0.0f, 1.0f);
    if (layerAlpha > 0.0f) linear = over(linear, p.blankingColour.rgb, layerAlpha);
  }
  for (int index = 0; index < p.layerCount; ++index) {
    constant Layer& layer = layers[index];
    if (layer.constrainToCell != 0 &&
        (coordinate.x < layer.cellBounds.x || coordinate.x >= layer.cellBounds.z ||
         coordinate.y < layer.cellBounds.y || coordinate.y >= layer.cellBounds.w)) continue;
    int2 local = coordinate - layer.geometry.xy;
    if (local.x < 0 || local.y < 0 ||
        local.x >= layer.geometry.z || local.y >= layer.geometry.w) continue;
    float coverage = float(masks[layer.maskOffset +
        local.y * layer.geometry.z + local.x]) / 255.0f;
    float layerAlpha = clamp(coverage * layer.opacity * layer.colour.a,
                             0.0f, 1.0f);
    if (layerAlpha > 0.0f) linear = over(linear, layer.colour.rgb, layerAlpha);
  }
  return linear;
}

void writeManagedPixel(device uchar* outputPixel, float4 linear,
                       constant Params& p) {
  float outputAlpha = linear.a;
  float3 straightLinear = outputAlpha > 1.0e-8f ?
      linear.rgb / outputAlpha : float3(0.0f);
  float3 outputEncoded = encodeDisplay(straightLinear, p);
  if (p.outputPremultiplied != 0) outputEncoded *= outputAlpha;
  writeChannel(outputPixel, 0, p.channelType, outputEncoded.r);
  writeChannel(outputPixel, 1, p.channelType, outputEncoded.g);
  writeChannel(outputPixel, 2, p.channelType, outputEncoded.b);
  writeChannel(outputPixel, 3, p.channelType, outputAlpha);
}

kernel void wipReviewIdentity(
    device const uchar* source [[buffer(0)]],
    device uchar* output [[buffer(1)]],
    device const uchar* masks [[buffer(2)]],
    constant Params& p [[buffer(3)]],
    constant Layer* layers [[buffer(4)]],
    uint2 grid [[thread_position_in_grid]]) {
  int2 coordinate = int2(grid) + p.renderWindow.xy;
  if (coordinate.x >= p.renderWindow.z || coordinate.y >= p.renderWindow.w) return;
  if (coordinate.x < p.sourceBounds.x || coordinate.x >= p.sourceBounds.z ||
      coordinate.y < p.sourceBounds.y || coordinate.y >= p.sourceBounds.w ||
      coordinate.x < p.outputBounds.x || coordinate.x >= p.outputBounds.z ||
      coordinate.y < p.outputBounds.y || coordinate.y >= p.outputBounds.w) return;

  device const uchar* sourcePixel = source +
      (coordinate.y - p.sourceBounds.y) * p.sourceRowBytes +
      (coordinate.x - p.sourceBounds.x) * p.sourcePixelBytes;
  device uchar* outputPixel = output +
      (coordinate.y - p.outputBounds.y) * p.outputRowBytes +
      (coordinate.x - p.outputBounds.x) * p.outputPixelBytes;
  float4 encoded = float4(readChannel(sourcePixel, 0, p.channelType),
                          readChannel(sourcePixel, 1, p.channelType),
                          readChannel(sourcePixel, 2, p.channelType),
                          readChannel(sourcePixel, 3, p.channelType));
  float alpha = encoded.a;
  float3 straightEncoded = p.sourcePremultiplied != 0 && alpha > 1.0e-8f
      ? encoded.rgb / alpha : encoded.rgb;
  float4 linear = float4(decodeDisplay(straightEncoded, p) * alpha, alpha);

  linear = applyOverlays(linear, coordinate, masks, p, layers);
  writeManagedPixel(outputPixel, linear, p);
}

kernel void wipReviewDecodeSource(
    device const uchar* source [[buffer(0)]],
    device float4* decoded [[buffer(1)]],
    constant Params& p [[buffer(3)]],
    uint2 grid [[thread_position_in_grid]]) {
  int width = p.sourceBounds.z - p.sourceBounds.x;
  int height = p.sourceBounds.w - p.sourceBounds.y;
  if (grid.x >= uint(width) || grid.y >= uint(height)) return;
  device const uchar* pixel = source + int(grid.y) * p.sourceRowBytes +
      int(grid.x) * p.sourcePixelBytes;
  float4 encoded = float4(readChannel(pixel, 0, p.channelType),
                          readChannel(pixel, 1, p.channelType),
                          readChannel(pixel, 2, p.channelType),
                          readChannel(pixel, 3, p.channelType));
  float alpha = encoded.a;
  float3 straight = p.sourcePremultiplied != 0 && alpha > 1.0e-8f ?
      encoded.rgb / alpha : encoded.rgb;
  decoded[grid.y * uint(width) + grid.x] =
      float4(decodeDisplay(straight, p) * alpha, alpha);
}

kernel void wipReviewResampled(
    device const float4* decoded [[buffer(0)]],
    device uchar* output [[buffer(1)]],
    device const uchar* masks [[buffer(2)]],
    constant Params& p [[buffer(3)]],
    constant Layer* layers [[buffer(4)]],
    uint2 grid [[thread_position_in_grid]]) {
  int2 coordinate = int2(grid) + p.renderWindow.xy;
  if (coordinate.x >= p.renderWindow.z || coordinate.y >= p.renderWindow.w ||
      coordinate.x < p.outputBounds.x || coordinate.x >= p.outputBounds.z ||
      coordinate.y < p.outputBounds.y || coordinate.y >= p.outputBounds.w) return;
  device uchar* outputPixel = output +
      (coordinate.y - p.outputBounds.y) * p.outputRowBytes +
      (coordinate.x - p.outputBounds.x) * p.outputPixelBytes;

  float sourceCenterX = p.transform.z;
  float sourceCenterY = p.transform.w;
  float outputCenterX = 0.5f * float(p.outputBounds.x + p.outputBounds.z);
  float outputCenterY = 0.5f * float(p.outputBounds.y + p.outputBounds.w);
  float canonicalX = sourceCenterX +
      (float(coordinate.x) + 0.5f - outputCenterX) / p.transform.x;
  float canonicalY = sourceCenterY +
      (float(coordinate.y) + 0.5f - outputCenterY) / p.transform.y;
  float4 linear = p.canvas;
  bool inside = canonicalX >= float(p.sourceBounds.x) &&
      canonicalX < float(p.sourceBounds.z) &&
      canonicalY >= float(p.sourceBounds.y) &&
      canonicalY < float(p.sourceBounds.w);
  if (inside) {
    float radius = p.filter == 0 ? 1.0f : (p.filter == 1 ? 2.0f : 3.0f);
    float filterScaleX = max(1.0f, 1.0f / abs(p.transform.x));
    float filterScaleY = max(1.0f, 1.0f / abs(p.transform.y));
    float sampleX = canonicalX - 0.5f;
    float sampleY = canonicalY - 0.5f;
    int firstX = int(ceil(sampleX - radius * filterScaleX));
    int lastX = int(floor(sampleX + radius * filterScaleX));
    int firstY = int(ceil(sampleY - radius * filterScaleY));
    int lastY = int(floor(sampleY + radius * filterScaleY));
    float4 sum = float4(0.0f);
    float weightSum = 0.0f;
    int sourceWidth = p.sourceBounds.z - p.sourceBounds.x;
    for (int sy = firstY; sy <= lastY; ++sy) {
      float wy = filterKernel((sampleY - float(sy)) / filterScaleY, p.filter);
      if (wy == 0.0f) continue;
      int clampedY = clamp(sy, p.sourceBounds.y, p.sourceBounds.w - 1);
      for (int sx = firstX; sx <= lastX; ++sx) {
        float wx = filterKernel((sampleX - float(sx)) / filterScaleX, p.filter);
        if (wx == 0.0f) continue;
        int clampedX = clamp(sx, p.sourceBounds.x, p.sourceBounds.z - 1);
        float weight = wx * wy;
        sum += decoded[(clampedY - p.sourceBounds.y) * sourceWidth +
                       clampedX - p.sourceBounds.x] * weight;
        weightSum += weight;
      }
    }
    linear = abs(weightSum) > 1.0e-12f ? sum / weightSum : float4(0.0f);
  }
  linear = applyOverlays(linear, coordinate, masks, p, layers);
  writeManagedPixel(outputPixel, linear, p);
}
)metal";

struct alignas(16) MetalParams {
  simd_int4 sourceBounds{};
  simd_int4 outputBounds{};
  simd_int4 renderWindow{};
  std::int32_t sourceRowBytes = 0;
  std::int32_t outputRowBytes = 0;
  std::int32_t sourcePixelBytes = 0;
  std::int32_t outputPixelBytes = 0;
  std::int32_t channelType = 0;
  std::int32_t sourcePremultiplied = 1;
  std::int32_t outputPremultiplied = 1;
  std::int32_t encoding = 0;
  float graphicsWhite = 100.0F;
  float peakNits = 1000.0F;
  std::int32_t filter = 0;
  simd_float4 transform{};
  simd_float4 canvas{};
  std::int32_t blankingEnabled = 0;
  float blankingOpacity = 1.0F;
  simd_float2 reserved0{};
  simd_float4 blankingColour{};
  simd_float4 aperture{};
  std::int32_t layerCount = 0;
  simd_int3 reserved1{};
};

struct alignas(16) MetalLayer {
  simd_int4 geometry{};
  simd_int4 cellBounds{};
  std::int32_t maskOffset = 0;
  std::int32_t constrainToCell = 0;
  simd_int2 reserved{};
  simd_float4 colour{};
  float opacity = 1.0F;
  simd_float3 reserved2{};
};

static_assert(offsetof(MetalParams, transform) == 96);
static_assert(offsetof(MetalParams, canvas) == 112);
static_assert(offsetof(MetalParams, blankingColour) == 144);
static_assert(offsetof(MetalParams, aperture) == 160);
static_assert(sizeof(MetalParams) == 208);
static_assert(sizeof(MetalLayer) == 96);

std::mutex gPipelineMutex;
id<MTLDevice> gPipelineDevice = nil;
id<MTLComputePipelineState> gIdentityPipeline = nil;
id<MTLComputePipelineState> gDecodePipeline = nil;
id<MTLComputePipelineState> gResamplePipeline = nil;

bool pipelinesFor(id<MTLDevice> device, std::string &error) {
  std::lock_guard<std::mutex> lock(gPipelineMutex);
  if (gIdentityPipeline && gDecodePipeline && gResamplePipeline &&
      gPipelineDevice == device)
    return true;
  NSError *compileError = nil;
  NSString *source = [[NSString alloc] initWithBytes:kMetalSource
                                              length:sizeof(kMetalSource) - 1
                                            encoding:NSUTF8StringEncoding];
  id<MTLLibrary> library = [device newLibraryWithSource:source
                                                options:nil
                                                  error:&compileError];
  if (!library) {
    error = compileError ? compileError.localizedDescription.UTF8String
                         : "Metal library compilation failed";
    return false;
  }
  auto createPipeline = [&](NSString *name,
                            id<MTLComputePipelineState> __strong &destination) {
    id<MTLFunction> function = [library newFunctionWithName:name];
    if (!function) {
      error = std::string("Metal kernel not found: ") + name.UTF8String;
      return false;
    }
    NSError *pipelineError = nil;
    destination = [device newComputePipelineStateWithFunction:function
                                                        error:&pipelineError];
    if (!destination) {
      error = pipelineError ? pipelineError.localizedDescription.UTF8String
                            : "Metal pipeline creation failed";
      return false;
    }
    return true;
  };
  gIdentityPipeline = nil;
  gDecodePipeline = nil;
  gResamplePipeline = nil;
  if (!createPipeline(@"wipReviewIdentity", gIdentityPipeline) ||
      !createPipeline(@"wipReviewDecodeSource", gDecodePipeline) ||
      !createPipeline(@"wipReviewResampled", gResamplePipeline)) {
    return false;
  }
  gPipelineDevice = device;
  return true;
}

int channelTypeIndex(probe::ChannelType type) noexcept {
  switch (type) {
  case probe::ChannelType::UInt8:
    return 0;
  case probe::ChannelType::UInt16:
    return 1;
  case probe::ChannelType::Half:
    return 2;
  case probe::ChannelType::Float32:
    return 3;
  }
  return -1;
}

} // namespace

RenderStatus renderMetal(const RenderRequest &request,
                         std::string &error) noexcept {
  @autoreleasepool {
    try {
      error.clear();
      if (!request.sourceBuffer || !request.outputBuffer ||
          !request.commandQueue) {
        error = "Metal render handles are incomplete";
        return RenderStatus::Unsupported;
      }
      if (request.sourceFormat.channels != 4 ||
          request.outputFormat.channels != 4 ||
          request.sourceFormat.channelType !=
              request.outputFormat.channelType ||
          request.sourceFormat.pixelBytes != request.outputFormat.pixelBytes ||
          request.sourceFormat.rowBytes <= 0 ||
          request.outputFormat.rowBytes <= 0) {
        error = "Metal render requires matching RGBA formats and positive row "
                "bytes";
        return RenderStatus::Unsupported;
      }
      id<MTLCommandQueue> queue =
          (__bridge id<MTLCommandQueue>)request.commandQueue;
      id<MTLDevice> device = queue.device;
      if (!pipelinesFor(device, error))
        return RenderStatus::Failed;

      std::vector<std::uint8_t> masks;
      std::vector<MetalLayer> layers;
      for (const auto &sourceLayer : request.layers) {
        if (!sourceLayer.mask.data || sourceLayer.mask.width <= 0 ||
            sourceLayer.mask.height <= 0 ||
            sourceLayer.mask.rowBytes < sourceLayer.mask.width)
          continue;
        MetalLayer layer;
        layer.geometry = {sourceLayer.origin.x, sourceLayer.origin.y,
                          sourceLayer.mask.width, sourceLayer.mask.height};
        layer.cellBounds = {
            sourceLayer.cellBounds.x1, sourceLayer.cellBounds.y1,
            sourceLayer.cellBounds.x2, sourceLayer.cellBounds.y2};
        layer.maskOffset = static_cast<std::int32_t>(masks.size());
        layer.constrainToCell = sourceLayer.constrainToCell ? 1 : 0;
        layer.colour = {sourceLayer.colour[0], sourceLayer.colour[1],
                        sourceLayer.colour[2], sourceLayer.colour[3]};
        layer.opacity = sourceLayer.opacity;
        for (int y = 0; y < sourceLayer.mask.height; ++y) {
          const auto *row =
              sourceLayer.mask.data +
              static_cast<std::ptrdiff_t>(y) * sourceLayer.mask.rowBytes;
          masks.insert(masks.end(), row, row + sourceLayer.mask.width);
        }
        layers.push_back(layer);
      }
      const std::int32_t layerCount = static_cast<std::int32_t>(layers.size());
      if (masks.empty())
        masks.push_back(0);
      if (layers.empty())
        layers.emplace_back();

      MetalParams params;
      const auto &sourceBounds = request.sourceFormat.bounds;
      const auto &outputBounds = request.outputFormat.bounds;
      const auto &window = request.renderWindow;
      params.sourceBounds = {sourceBounds.x1, sourceBounds.y1, sourceBounds.x2,
                             sourceBounds.y2};
      params.outputBounds = {outputBounds.x1, outputBounds.y1, outputBounds.x2,
                             outputBounds.y2};
      params.renderWindow = {window.x1, window.y1, window.x2, window.y2};
      params.sourceRowBytes =
          static_cast<std::int32_t>(request.sourceFormat.rowBytes);
      params.outputRowBytes =
          static_cast<std::int32_t>(request.outputFormat.rowBytes);
      params.sourcePixelBytes =
          static_cast<std::int32_t>(request.sourceFormat.pixelBytes);
      params.outputPixelBytes =
          static_cast<std::int32_t>(request.outputFormat.pixelBytes);
      params.channelType = channelTypeIndex(request.sourceFormat.channelType);
      params.sourcePremultiplied = request.sourcePremultiplied ? 1 : 0;
      params.outputPremultiplied = request.outputPremultiplied ? 1 : 0;
      params.encoding = static_cast<std::int32_t>(request.color.encoding);
      params.graphicsWhite =
          static_cast<float>(request.color.graphicsWhiteNits);
      params.peakNits = static_cast<float>(request.color.peakNits);
      params.filter = static_cast<std::int32_t>(request.renderOptions.filter);
      const auto transform = probe::computePlacement(sourceBounds, outputBounds,
                                                     request.renderOptions);
      params.transform = {static_cast<float>(transform.scaleX),
                          static_cast<float>(transform.scaleY),
                          static_cast<float>(transform.sourceCenterX),
                          static_cast<float>(transform.sourceCenterY)};
      const float canvasAlpha = request.renderOptions.canvas[3];
      params.canvas = {request.renderOptions.canvas[0] * canvasAlpha,
                       request.renderOptions.canvas[1] * canvasAlpha,
                       request.renderOptions.canvas[2] * canvasAlpha,
                       canvasAlpha};
      params.blankingEnabled = request.blanking.enabled ? 1 : 0;
      params.blankingOpacity = request.blanking.opacity;
      params.blankingColour = {
          request.blanking.colour[0], request.blanking.colour[1],
          request.blanking.colour[2], request.blanking.colour[3]};
      const auto aperture =
          probe::computeBlankingAperture(outputBounds, request.blanking);
      params.aperture = {
          static_cast<float>(aperture.x1), static_cast<float>(aperture.y1),
          static_cast<float>(aperture.x2), static_cast<float>(aperture.y2)};
      params.layerCount = layerCount;

      id<MTLBuffer> maskBuffer =
          [device newBufferWithBytes:masks.data()
                              length:masks.size()
                             options:MTLResourceStorageModeShared];
      id<MTLBuffer> paramsBuffer =
          [device newBufferWithBytes:&params
                              length:sizeof(params)
                             options:MTLResourceStorageModeShared];
      id<MTLBuffer> layerBuffer =
          [device newBufferWithBytes:layers.data()
                              length:layers.size() * sizeof(MetalLayer)
                             options:MTLResourceStorageModeShared];
      id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
      if (!maskBuffer || !paramsBuffer || !layerBuffer || !commandBuffer) {
        error = "Metal resource allocation failed";
        return RenderStatus::Failed;
      }

      auto dispatch = [&](id<MTLComputeCommandEncoder> encoder,
                          id<MTLComputePipelineState> pipeline,
                          NSUInteger width, NSUInteger height) {
        const NSUInteger groupWidth =
            std::min<NSUInteger>(16, pipeline.threadExecutionWidth);
        const NSUInteger groupHeight = std::max<NSUInteger>(
            1, std::min<NSUInteger>(16, pipeline.maxTotalThreadsPerThreadgroup /
                                            groupWidth));
        [encoder dispatchThreads:MTLSizeMake(width, height, 1)
            threadsPerThreadgroup:MTLSizeMake(groupWidth, groupHeight, 1)];
      };

      const NSUInteger outputWidth =
          static_cast<NSUInteger>(std::max(0, window.x2 - window.x1));
      const NSUInteger outputHeight =
          static_cast<NSUInteger>(std::max(0, window.y2 - window.y1));
      const bool identity = probe::isEffectivelyIdentityPlacement(
          sourceBounds, outputBounds, request.renderOptions);
      if (identity) {
        id<MTLComputeCommandEncoder> encoder =
            [commandBuffer computeCommandEncoder];
        if (!encoder) {
          error = "Metal identity encoder allocation failed";
          return RenderStatus::Failed;
        }
        [encoder setComputePipelineState:gIdentityPipeline];
        [encoder setBuffer:(__bridge id<MTLBuffer>)request.sourceBuffer
                    offset:0
                   atIndex:0];
        [encoder setBuffer:(__bridge id<MTLBuffer>)request.outputBuffer
                    offset:0
                   atIndex:1];
        [encoder setBuffer:maskBuffer offset:0 atIndex:2];
        [encoder setBuffer:paramsBuffer offset:0 atIndex:3];
        [encoder setBuffer:layerBuffer offset:0 atIndex:4];
        dispatch(encoder, gIdentityPipeline, outputWidth, outputHeight);
        [encoder endEncoding];
      } else {
        const std::size_t sourceWidth = static_cast<std::size_t>(
            std::max(0, sourceBounds.x2 - sourceBounds.x1));
        const std::size_t sourceHeight = static_cast<std::size_t>(
            std::max(0, sourceBounds.y2 - sourceBounds.y1));
        if (sourceWidth == 0 || sourceHeight == 0 ||
            sourceWidth > std::numeric_limits<std::size_t>::max() /
                              sourceHeight / sizeof(simd_float4)) {
          error = "Metal decoded-source dimensions are invalid";
          return RenderStatus::Unsupported;
        }
        const std::size_t decodedBytes =
            sourceWidth * sourceHeight * sizeof(simd_float4);
        id<MTLBuffer> decodedBuffer =
            [device newBufferWithLength:decodedBytes
                                options:MTLResourceStorageModePrivate];
        id<MTLComputeCommandEncoder> decodeEncoder =
            [commandBuffer computeCommandEncoder];
        if (!decodedBuffer || !decodeEncoder) {
          error = "Metal decoded-source allocation failed";
          return RenderStatus::Failed;
        }
        [decodeEncoder setComputePipelineState:gDecodePipeline];
        [decodeEncoder setBuffer:(__bridge id<MTLBuffer>)request.sourceBuffer
                          offset:0
                         atIndex:0];
        [decodeEncoder setBuffer:decodedBuffer offset:0 atIndex:1];
        [decodeEncoder setBuffer:paramsBuffer offset:0 atIndex:3];
        dispatch(decodeEncoder, gDecodePipeline, sourceWidth, sourceHeight);
        [decodeEncoder endEncoding];

        id<MTLComputeCommandEncoder> resampleEncoder =
            [commandBuffer computeCommandEncoder];
        if (!resampleEncoder) {
          error = "Metal resample encoder allocation failed";
          return RenderStatus::Failed;
        }
        [resampleEncoder setComputePipelineState:gResamplePipeline];
        [resampleEncoder setBuffer:decodedBuffer offset:0 atIndex:0];
        [resampleEncoder setBuffer:(__bridge id<MTLBuffer>)request.outputBuffer
                            offset:0
                           atIndex:1];
        [resampleEncoder setBuffer:maskBuffer offset:0 atIndex:2];
        [resampleEncoder setBuffer:paramsBuffer offset:0 atIndex:3];
        [resampleEncoder setBuffer:layerBuffer offset:0 atIndex:4];
        dispatch(resampleEncoder, gResamplePipeline, outputWidth, outputHeight);
        [resampleEncoder endEncoding];
      }
      [commandBuffer commit];
      return RenderStatus::Rendered;
    } catch (...) {
      error = "Unexpected Metal backend exception";
      return RenderStatus::Failed;
    }
  }
}

} // namespace wipreview::gpu
