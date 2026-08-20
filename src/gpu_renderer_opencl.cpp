#include "gpu_renderer.hpp"

#if !defined(_WIN32) && !defined(WIPREVIEW_OPENCL_SYNTAX_CHECK)
#error "The OpenCL backend is the Windows GPU implementation"
#endif

#define CL_TARGET_OPENCL_VERSION 110
#include <CL/cl.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace wipreview::gpu {
namespace {

constexpr char kOpenCLSource[] = R"opencl(
typedef struct {
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
} Params;

typedef struct {
  int4 geometry;
  int4 mask;
  float4 colour;
  float4 compositing;
} Layer;

float halfToFloatBits(ushort value) {
  uint sign = ((uint)value & 0x8000u) << 16u;
  uint exponent = ((uint)value >> 10u) & 0x1fu;
  uint mantissa = (uint)value & 0x03ffu;
  uint result;
  if (exponent == 0u) {
    if (mantissa == 0u) return as_float(sign);
    int shift = 0;
    while ((mantissa & 0x0400u) == 0u) { mantissa <<= 1u; ++shift; }
    mantissa &= 0x03ffu;
    result = sign | (uint)(127 - 15 - shift) << 23u | mantissa << 13u;
  } else if (exponent == 0x1fu) {
    result = sign | 0x7f800000u | mantissa << 13u;
  } else {
    result = sign | (exponent + 112u) << 23u | mantissa << 13u;
  }
  return as_float(result);
}

ushort floatToHalfBits(float value) {
  uint bits = as_uint(value);
  uint sign = (bits >> 16u) & 0x8000u;
  uint mantissa = bits & 0x007fffffu;
  int exponent = (int)((bits >> 23u) & 0xffu) - 127 + 15;
  if (exponent <= 0) {
    if (exponent < -10) return (ushort)sign;
    uint rounded = (mantissa | 0x00800000u) >> (uint)(14 - exponent);
    return (ushort)(sign | rounded);
  }
  if (exponent >= 31) return (ushort)(sign | 0x7c00u);
  return (ushort)(sign | (uint)exponent << 10u |
                  (mantissa + 0x00001000u) >> 13u);
}

float readChannel(__global const uchar* pixel, int channel, int type) {
  if (type == 0) return (float)pixel[channel] / 255.0f;
  if (type == 1) return (float)((__global const ushort*)pixel)[channel] / 65535.0f;
  if (type == 2) return halfToFloatBits(((__global const ushort*)pixel)[channel]);
  return ((__global const float*)pixel)[channel];
}

void writeChannel(__global uchar* pixel, int channel, int type, float value) {
  if (type == 0) pixel[channel] = (uchar)rint(clamp(value, 0.0f, 1.0f) * 255.0f);
  else if (type == 1) ((__global ushort*)pixel)[channel] =
      (ushort)rint(clamp(value, 0.0f, 1.0f) * 65535.0f);
  else if (type == 2) ((__global ushort*)pixel)[channel] = floatToHalfBits(value);
  else ((__global float*)pixel)[channel] = value;
}

float pqToNits(float encoded) {
  const float m1=2610.0f/16384.0f, m2=2523.0f/32.0f;
  const float c1=3424.0f/4096.0f, c2=2413.0f/128.0f, c3=2392.0f/128.0f;
  float value = pow(clamp(encoded,0.0f,1.0f),1.0f/m2);
  float numerator=max(value-c1,0.0f), denominator=c2-c3*value;
  return denominator<=0.0f ? 10000.0f :
      10000.0f*pow(numerator/denominator,1.0f/m1);
}
float nitsToPq(float nits) {
  const float m1=2610.0f/16384.0f, m2=2523.0f/32.0f;
  const float c1=3424.0f/4096.0f, c2=2413.0f/128.0f, c3=2392.0f/128.0f;
  float luminance=pow(clamp(nits/10000.0f,0.0f,1.0f),m1);
  return pow((c1+c2*luminance)/(1.0f+c3*luminance),m2);
}
float hlgInverse(float encoded) {
  const float a=0.17883277f,b=0.28466892f,c=0.55991073f;
  float value=max(encoded,0.0f);
  return value<=0.5f ? value*value/3.0f : (exp((value-c)/a)+b)/12.0f;
}
float hlgForward(float linear) {
  const float a=0.17883277f,b=0.28466892f,c=0.55991073f;
  float value=max(linear,0.0f);
  return value<=1.0f/12.0f ? sqrt(3.0f*value) : a*log(12.0f*value-b)+c;
}
float3 decodeDisplay(float3 encoded, __global const Params* p) {
  if (p->encoding==0) return sign(encoded)*pow(fabs(encoded),(float3)(2.4f));
  if (p->encoding==1) return (float3)(pqToNits(encoded.x),pqToNits(encoded.y),
      pqToNits(encoded.z))/p->graphicsWhite;
  float3 scene=(float3)(hlgInverse(encoded.x),hlgInverse(encoded.y),
                        hlgInverse(encoded.z));
  float luminance=max(dot(scene,(float3)(0.2627f,0.6780f,0.0593f)),0.0f);
  float gamma=1.2f+0.42f*log10(p->peakNits/1000.0f);
  float scale=p->peakNits*(luminance>0.0f?pow(luminance,gamma-1.0f):0.0f);
  return scene*scale/p->graphicsWhite;
}
float3 encodeDisplay(float3 linear, __global const Params* p) {
  if (p->encoding==0) return sign(linear)*pow(fabs(linear),(float3)(1.0f/2.4f));
  if (p->encoding==1) {
    float3 nits=linear*p->graphicsWhite;
    return (float3)(nitsToPq(nits.x),nitsToPq(nits.y),nitsToPq(nits.z));
  }
  float3 nits=max(linear*p->graphicsWhite,(float3)(0.0f));
  float luminance=max(dot(nits,(float3)(0.2627f,0.6780f,0.0593f)),0.0f);
  float gamma=1.2f+0.42f*log10(p->peakNits/1000.0f);
  float sceneLuminance=luminance>0.0f?pow(luminance/p->peakNits,1.0f/gamma):0.0f;
  float scale=sceneLuminance>0.0f?p->peakNits*pow(sceneLuminance,gamma-1.0f):1.0f;
  float3 scene=nits/scale;
  return (float3)(hlgForward(scene.x),hlgForward(scene.y),hlgForward(scene.z));
}
float blankingCoverage(int2 coordinate, __global const Params* p) {
  int horizontal=p->aperture.y>(float)p->outputBounds.y ||
      p->aperture.w<(float)p->outputBounds.w;
  if (horizontal) {
    float overlap=max(0.0f,min((float)(coordinate.y+1),p->aperture.w)-
        max((float)coordinate.y,p->aperture.y));
    return 1.0f-overlap;
  }
  float overlap=max(0.0f,min((float)(coordinate.x+1),p->aperture.z)-
      max((float)coordinate.x,p->aperture.x));
  return 1.0f-overlap;
}
float4 over(float4 base,float3 colour,float alpha) {
  return (float4)(colour*alpha+base.xyz*(1.0f-alpha),
                  alpha+base.w*(1.0f-alpha));
}
float filterKernel(float distance,int filter) {
  float x=fabs(distance);
  if(filter==0) return x<1.0f?1.0f-x:0.0f;
  if(filter==1) {
    if(x<1.0f) return 1.5f*x*x*x-2.5f*x*x+1.0f;
    if(x<2.0f) return -0.5f*x*x*x+2.5f*x*x-4.0f*x+2.0f;
    return 0.0f;
  }
  if(x>=3.0f) return 0.0f;
  if(x<1.0e-6f) return 1.0f;
  const float pi=3.14159265358979323846f;
  float pix=pi*x;
  return (sin(pix)/pix)*(sin(pix/3.0f)/(pix/3.0f));
}
float4 applyOverlays(float4 linear,int2 coordinate,__global const uchar* masks,
                     __global const Params* p,__global const Layer* layers) {
  if(p->blankingEnabled!=0) {
    float coverage=blankingCoverage(coordinate,p);
    float alpha=clamp(coverage*p->blankingOpacity*p->blankingColour.w,0.0f,1.0f);
    if(alpha>0.0f) linear=over(linear,p->blankingColour.xyz,alpha);
  }
  for(int index=0;index<p->layerCount;++index) {
    __global const Layer* layer=layers+index;
    int2 localCoord=coordinate-layer->geometry.xy;
    if(localCoord.x<0 || localCoord.y<0 || localCoord.x>=layer->geometry.z ||
       localCoord.y>=layer->geometry.w) continue;
    float coverage=(float)masks[layer->mask.x+localCoord.y*layer->geometry.z+localCoord.x]/255.0f;
    float alpha=clamp(coverage*layer->compositing.x*layer->colour.w,0.0f,1.0f);
    if(alpha>0.0f) linear=over(linear,layer->colour.xyz,alpha);
  }
  return linear;
}
void writeManagedPixel(__global uchar* output,float4 linear,__global const Params* p) {
  float alpha=linear.w;
  float3 straight=alpha>1.0e-8f?linear.xyz/alpha:(float3)(0.0f);
  float3 encoded=encodeDisplay(straight,p);
  if(p->outputPremultiplied!=0) encoded*=alpha;
  writeChannel(output,0,p->channelType,encoded.x);
  writeChannel(output,1,p->channelType,encoded.y);
  writeChannel(output,2,p->channelType,encoded.z);
  writeChannel(output,3,p->channelType,alpha);
}

__kernel void wipReviewIdentity(__global const uchar* source,__global uchar* output,
    __global const uchar* masks,__global const Params* p,__global const Layer* layers) {
  int2 coordinate=(int2)((int)get_global_id(0)+p->renderWindow.x,
                         (int)get_global_id(1)+p->renderWindow.y);
  if(coordinate.x>=p->renderWindow.z || coordinate.y>=p->renderWindow.w ||
     coordinate.x<p->sourceBounds.x || coordinate.x>=p->sourceBounds.z ||
     coordinate.y<p->sourceBounds.y || coordinate.y>=p->sourceBounds.w ||
     coordinate.x<p->outputBounds.x || coordinate.x>=p->outputBounds.z ||
     coordinate.y<p->outputBounds.y || coordinate.y>=p->outputBounds.w) return;
  __global const uchar* sp=source+(coordinate.y-p->sourceBounds.y)*p->sourceRowBytes+
      (coordinate.x-p->sourceBounds.x)*p->sourcePixelBytes;
  __global uchar* op=output+(coordinate.y-p->outputBounds.y)*p->outputRowBytes+
      (coordinate.x-p->outputBounds.x)*p->outputPixelBytes;
  float4 encoded=(float4)(readChannel(sp,0,p->channelType),readChannel(sp,1,p->channelType),
                          readChannel(sp,2,p->channelType),readChannel(sp,3,p->channelType));
  float alpha=encoded.w;
  float3 straight=p->sourcePremultiplied!=0 && alpha>1.0e-8f?encoded.xyz/alpha:encoded.xyz;
  float4 linear=(float4)(decodeDisplay(straight,p)*alpha,alpha);
  writeManagedPixel(op,applyOverlays(linear,coordinate,masks,p,layers),p);
}

__kernel void wipReviewDecodeSource(__global const uchar* source,__global float4* decoded,
                                    __global const Params* p) {
  int x=(int)get_global_id(0), y=(int)get_global_id(1);
  int width=p->sourceBounds.z-p->sourceBounds.x;
  int height=p->sourceBounds.w-p->sourceBounds.y;
  if(x>=width || y>=height) return;
  __global const uchar* sp=source+y*p->sourceRowBytes+x*p->sourcePixelBytes;
  float4 encoded=(float4)(readChannel(sp,0,p->channelType),readChannel(sp,1,p->channelType),
                          readChannel(sp,2,p->channelType),readChannel(sp,3,p->channelType));
  float alpha=encoded.w;
  float3 straight=p->sourcePremultiplied!=0 && alpha>1.0e-8f?encoded.xyz/alpha:encoded.xyz;
  decoded[y*width+x]=(float4)(decodeDisplay(straight,p)*alpha,alpha);
}

__kernel void wipReviewResampled(__global const float4* decoded,__global uchar* output,
    __global const uchar* masks,__global const Params* p,__global const Layer* layers) {
  int2 coordinate=(int2)((int)get_global_id(0)+p->renderWindow.x,
                         (int)get_global_id(1)+p->renderWindow.y);
  if(coordinate.x>=p->renderWindow.z || coordinate.y>=p->renderWindow.w ||
     coordinate.x<p->outputBounds.x || coordinate.x>=p->outputBounds.z ||
     coordinate.y<p->outputBounds.y || coordinate.y>=p->outputBounds.w) return;
  __global uchar* op=output+(coordinate.y-p->outputBounds.y)*p->outputRowBytes+
      (coordinate.x-p->outputBounds.x)*p->outputPixelBytes;
  float outX=0.5f*(float)(p->outputBounds.x+p->outputBounds.z);
  float outY=0.5f*(float)(p->outputBounds.y+p->outputBounds.w);
  float canonicalX=p->transform.z+((float)coordinate.x+0.5f-outX)/p->transform.x;
  float canonicalY=p->transform.w+((float)coordinate.y+0.5f-outY)/p->transform.y;
  float4 linear=p->canvas;
  int inside=canonicalX>=(float)p->sourceBounds.x && canonicalX<(float)p->sourceBounds.z &&
             canonicalY>=(float)p->sourceBounds.y && canonicalY<(float)p->sourceBounds.w;
  if(inside) {
    float radius=p->filter==0?1.0f:(p->filter==1?2.0f:3.0f);
    float fsx=max(1.0f,1.0f/fabs(p->transform.x));
    float fsy=max(1.0f,1.0f/fabs(p->transform.y));
    float sxp=canonicalX-0.5f,syp=canonicalY-0.5f;
    int firstX=(int)ceil(sxp-radius*fsx),lastX=(int)floor(sxp+radius*fsx);
    int firstY=(int)ceil(syp-radius*fsy),lastY=(int)floor(syp+radius*fsy);
    float4 sum=(float4)(0.0f); float weightSum=0.0f;
    int width=p->sourceBounds.z-p->sourceBounds.x;
    for(int sy=firstY;sy<=lastY;++sy) {
      float wy=filterKernel((syp-(float)sy)/fsy,p->filter); if(wy==0.0f) continue;
      int cy=clamp(sy,p->sourceBounds.y,p->sourceBounds.w-1);
      for(int sx=firstX;sx<=lastX;++sx) {
        float wx=filterKernel((sxp-(float)sx)/fsx,p->filter); if(wx==0.0f) continue;
        int cx=clamp(sx,p->sourceBounds.x,p->sourceBounds.z-1);
        float weight=wx*wy;
        sum+=decoded[(cy-p->sourceBounds.y)*width+cx-p->sourceBounds.x]*weight;
        weightSum+=weight;
      }
    }
    linear=fabs(weightSum)>1.0e-12f?sum/weightSum:(float4)(0.0f);
  }
  writeManagedPixel(op,applyOverlays(linear,coordinate,masks,p,layers),p);
}
)opencl";

struct alignas(16) Int4 {
  std::int32_t x = 0, y = 0, z = 0, w = 0;
};
struct alignas(8) Float2 {
  float x = 0, y = 0;
};
struct alignas(16) Float4 {
  float x = 0, y = 0, z = 0, w = 0;
};
struct alignas(16) Int3 {
  std::int32_t x = 0, y = 0, z = 0, pad = 0;
};
struct alignas(16) OpenCLParams {
  Int4 sourceBounds{}, outputBounds{}, renderWindow{};
  std::int32_t sourceRowBytes = 0, outputRowBytes = 0, sourcePixelBytes = 0,
               outputPixelBytes = 0;
  std::int32_t channelType = 0, sourcePremultiplied = 1,
               outputPremultiplied = 1, encoding = 0;
  float graphicsWhite = 100.0F, peakNits = 1000.0F;
  std::int32_t filter = 0;
  Float4 transform{}, canvas{};
  std::int32_t blankingEnabled = 0;
  float blankingOpacity = 1.0F;
  Float2 reserved0{};
  Float4 blankingColour{}, aperture{};
  std::int32_t layerCount = 0;
  Int3 reserved1{};
};
struct alignas(16) OpenCLLayer {
  Int4 geometry{}, mask{};
  Float4 colour{};
  Float4 compositing{};
};
static_assert(sizeof(OpenCLParams) == 208);
static_assert(sizeof(OpenCLLayer) == 64);

struct OpenCLApi {
  HMODULE module = nullptr;
  decltype(&::clGetCommandQueueInfo) getQueueInfo = nullptr;
  decltype(&::clCreateProgramWithSource) createProgram = nullptr;
  decltype(&::clBuildProgram) buildProgram = nullptr;
  decltype(&::clGetProgramBuildInfo) getBuildInfo = nullptr;
  decltype(&::clCreateKernel) createKernel = nullptr;
  decltype(&::clSetKernelArg) setKernelArg = nullptr;
  decltype(&::clCreateBuffer) createBuffer = nullptr;
  decltype(&::clEnqueueNDRangeKernel) enqueueKernel = nullptr;
  decltype(&::clReleaseMemObject) releaseMem = nullptr;
  decltype(&::clReleaseKernel) releaseKernel = nullptr;
  decltype(&::clReleaseProgram) releaseProgram = nullptr;
};

OpenCLApi &api() {
  static OpenCLApi value;
  static std::once_flag once;
  std::call_once(once, [&] {
    value.module = LoadLibraryW(L"OpenCL.dll");
    if (!value.module)
      return;
#define WIP_LOAD(member, name)                                                 \
  value.member = reinterpret_cast<decltype(value.member)>(                     \
      GetProcAddress(value.module, name))
    WIP_LOAD(getQueueInfo, "clGetCommandQueueInfo");
    WIP_LOAD(createProgram, "clCreateProgramWithSource");
    WIP_LOAD(buildProgram, "clBuildProgram");
    WIP_LOAD(getBuildInfo, "clGetProgramBuildInfo");
    WIP_LOAD(createKernel, "clCreateKernel");
    WIP_LOAD(setKernelArg, "clSetKernelArg");
    WIP_LOAD(createBuffer, "clCreateBuffer");
    WIP_LOAD(enqueueKernel, "clEnqueueNDRangeKernel");
    WIP_LOAD(releaseMem, "clReleaseMemObject");
    WIP_LOAD(releaseKernel, "clReleaseKernel");
    WIP_LOAD(releaseProgram, "clReleaseProgram");
#undef WIP_LOAD
  });
  return value;
}

bool apiReady(const OpenCLApi &a) {
  return a.module && a.getQueueInfo && a.createProgram && a.buildProgram &&
         a.getBuildInfo && a.createKernel && a.setKernelArg && a.createBuffer &&
         a.enqueueKernel && a.releaseMem && a.releaseKernel && a.releaseProgram;
}

std::mutex gProgramMutex;
std::map<cl_context, cl_program> gPrograms;

cl_program programFor(cl_command_queue queue, cl_device_id device,
                      std::string &error) {
  auto &a = api();
  cl_context context = nullptr;
  cl_int status = a.getQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof(context),
                                 &context, nullptr);
  if (status != CL_SUCCESS || !context) {
    error = "OpenCL queue context unavailable: " + std::to_string(status);
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(gProgramMutex);
  auto found = gPrograms.find(context);
  if (found != gPrograms.end())
    return found->second;
  const char *source = kOpenCLSource;
  size_t length = sizeof(kOpenCLSource) - 1;
  cl_program program = a.createProgram(context, 1, &source, &length, &status);
  if (status != CL_SUCCESS || !program) {
    error = "OpenCL program creation failed: " + std::to_string(status);
    return nullptr;
  }
  status =
      a.buildProgram(program, 1, &device, "-cl-std=CL1.1", nullptr, nullptr);
  if (status != CL_SUCCESS) {
    size_t size = 0;
    a.getBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size);
    std::string log(size, '\0');
    if (size)
      a.getBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, size, log.data(),
                     nullptr);
    error =
        "OpenCL program build failed: " + std::to_string(status) + " " + log;
    a.releaseProgram(program);
    return nullptr;
  }
  gPrograms.emplace(context, program);
  return program;
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

RenderStatus renderOpenCL(const RenderRequest &request,
                          std::string &error) noexcept {
  error.clear();
  auto &a = api();
  if (!apiReady(a)) {
    error = "OpenCL.dll or required OpenCL 1.1 entry points are unavailable";
    return RenderStatus::Unsupported;
  }
  if (!request.sourceBuffer || !request.outputBuffer || !request.commandQueue) {
    error = "OpenCL render handles are incomplete";
    return RenderStatus::Unsupported;
  }
  if (request.sourceFormat.channels != 4 ||
      request.outputFormat.channels != 4 ||
      request.sourceFormat.channelType != request.outputFormat.channelType ||
      request.sourceFormat.pixelBytes != request.outputFormat.pixelBytes ||
      request.sourceFormat.rowBytes <= 0 ||
      request.outputFormat.rowBytes <= 0) {
    error =
        "OpenCL render requires matching RGBA formats and positive row bytes";
    return RenderStatus::Unsupported;
  }
  cl_command_queue queue =
      reinterpret_cast<cl_command_queue>(request.commandQueue);
  cl_mem source = reinterpret_cast<cl_mem>(request.sourceBuffer);
  cl_mem output = reinterpret_cast<cl_mem>(request.outputBuffer);
  cl_device_id device = nullptr;
  cl_int status =
      a.getQueueInfo(queue, CL_QUEUE_DEVICE, sizeof(device), &device, nullptr);
  if (status != CL_SUCCESS || !device) {
    error = "OpenCL queue device unavailable: " + std::to_string(status);
    return RenderStatus::Failed;
  }
  cl_program program = programFor(queue, device, error);
  if (!program)
    return RenderStatus::Failed;

  std::vector<std::uint8_t> masks;
  std::vector<OpenCLLayer> layers;
  for (const auto &sourceLayer : request.layers) {
    if (!sourceLayer.mask.data || sourceLayer.mask.width <= 0 ||
        sourceLayer.mask.height <= 0 ||
        sourceLayer.mask.rowBytes < sourceLayer.mask.width)
      continue;
    OpenCLLayer layer;
    layer.geometry = {sourceLayer.origin.x, sourceLayer.origin.y,
                      sourceLayer.mask.width, sourceLayer.mask.height};
    layer.mask.x = static_cast<std::int32_t>(masks.size());
    layer.colour = {sourceLayer.colour[0], sourceLayer.colour[1],
                    sourceLayer.colour[2], sourceLayer.colour[3]};
    layer.compositing.x = sourceLayer.opacity;
    for (int y = 0; y < sourceLayer.mask.height; ++y) {
      const auto *row = sourceLayer.mask.data + static_cast<std::ptrdiff_t>(y) *
                                                    sourceLayer.mask.rowBytes;
      masks.insert(masks.end(), row, row + sourceLayer.mask.width);
    }
    layers.push_back(layer);
  }
  const std::int32_t layerCount = static_cast<std::int32_t>(layers.size());
  if (masks.empty())
    masks.push_back(0);
  if (layers.empty())
    layers.emplace_back();
  OpenCLParams p;
  const auto &sb = request.sourceFormat.bounds;
  const auto &ob = request.outputFormat.bounds;
  const auto &rw = request.renderWindow;
  p.sourceBounds = {sb.x1, sb.y1, sb.x2, sb.y2};
  p.outputBounds = {ob.x1, ob.y1, ob.x2, ob.y2};
  p.renderWindow = {rw.x1, rw.y1, rw.x2, rw.y2};
  p.sourceRowBytes = static_cast<std::int32_t>(request.sourceFormat.rowBytes);
  p.outputRowBytes = static_cast<std::int32_t>(request.outputFormat.rowBytes);
  p.sourcePixelBytes =
      static_cast<std::int32_t>(request.sourceFormat.pixelBytes);
  p.outputPixelBytes =
      static_cast<std::int32_t>(request.outputFormat.pixelBytes);
  p.channelType = channelTypeIndex(request.sourceFormat.channelType);
  p.sourcePremultiplied = request.sourcePremultiplied ? 1 : 0;
  p.outputPremultiplied = request.outputPremultiplied ? 1 : 0;
  p.encoding = static_cast<std::int32_t>(request.color.encoding);
  p.graphicsWhite = static_cast<float>(request.color.graphicsWhiteNits);
  p.peakNits = static_cast<float>(request.color.peakNits);
  p.filter = static_cast<std::int32_t>(request.renderOptions.filter);
  const auto t = probe::computePlacement(sb, ob, request.renderOptions);
  p.transform = {static_cast<float>(t.scaleX), static_cast<float>(t.scaleY),
                 static_cast<float>(t.sourceCenterX),
                 static_cast<float>(t.sourceCenterY)};
  const float ca = request.renderOptions.canvas[3];
  p.canvas = {request.renderOptions.canvas[0] * ca,
              request.renderOptions.canvas[1] * ca,
              request.renderOptions.canvas[2] * ca, ca};
  p.blankingEnabled = request.blanking.enabled ? 1 : 0;
  p.blankingOpacity = request.blanking.opacity;
  p.blankingColour = {request.blanking.colour[0], request.blanking.colour[1],
                      request.blanking.colour[2], request.blanking.colour[3]};
  const auto aperture = probe::computeBlankingAperture(ob, request.blanking);
  p.aperture = {
      static_cast<float>(aperture.x1), static_cast<float>(aperture.y1),
      static_cast<float>(aperture.x2), static_cast<float>(aperture.y2)};
  p.layerCount = layerCount;

  cl_context context = nullptr;
  a.getQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof(context), &context, nullptr);
  cl_mem maskBuffer =
      a.createBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     masks.size(), masks.data(), &status);
  cl_mem paramsBuffer = a.createBuffer(
      context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(p), &p, &status);
  cl_mem layerBuffer = a.createBuffer(
      context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
      layers.size() * sizeof(OpenCLLayer), layers.data(), &status);
  if (!maskBuffer || !paramsBuffer || !layerBuffer || status != CL_SUCCESS) {
    error =
        "OpenCL constant-buffer allocation failed: " + std::to_string(status);
    if (maskBuffer)
      a.releaseMem(maskBuffer);
    if (paramsBuffer)
      a.releaseMem(paramsBuffer);
    if (layerBuffer)
      a.releaseMem(layerBuffer);
    return RenderStatus::Failed;
  }
  auto releaseConstants = [&] {
    a.releaseMem(maskBuffer);
    a.releaseMem(paramsBuffer);
    a.releaseMem(layerBuffer);
  };
  const size_t outputWork[2] = {
      static_cast<size_t>(std::max(0, rw.x2 - rw.x1)),
      static_cast<size_t>(std::max(0, rw.y2 - rw.y1))};
  const bool identity =
      probe::isEffectivelyIdentityPlacement(sb, ob, request.renderOptions);
  if (identity) {
    cl_kernel kernel = a.createKernel(program, "wipReviewIdentity", &status);
    if (!kernel) {
      releaseConstants();
      error =
          "OpenCL identity kernel creation failed: " + std::to_string(status);
      return RenderStatus::Failed;
    }
    status = a.setKernelArg(kernel, 0, sizeof(source), &source);
    status |= a.setKernelArg(kernel, 1, sizeof(output), &output);
    status |= a.setKernelArg(kernel, 2, sizeof(maskBuffer), &maskBuffer);
    status |= a.setKernelArg(kernel, 3, sizeof(paramsBuffer), &paramsBuffer);
    status |= a.setKernelArg(kernel, 4, sizeof(layerBuffer), &layerBuffer);
    if (status == CL_SUCCESS)
      status = a.enqueueKernel(queue, kernel, 2, nullptr, outputWork, nullptr,
                               0, nullptr, nullptr);
    a.releaseKernel(kernel);
    releaseConstants();
    if (status != CL_SUCCESS) {
      error = "OpenCL identity dispatch failed: " + std::to_string(status);
      return RenderStatus::Failed;
    }
    return RenderStatus::Rendered;
  }
  const size_t sw = static_cast<size_t>(std::max(0, sb.x2 - sb.x1)),
               sh = static_cast<size_t>(std::max(0, sb.y2 - sb.y1));
  if (sw == 0 || sh == 0 ||
      sw > std::numeric_limits<size_t>::max() / sh / sizeof(Float4)) {
    releaseConstants();
    error = "OpenCL decoded-source dimensions are invalid";
    return RenderStatus::Unsupported;
  }
  cl_mem decoded = a.createBuffer(context, CL_MEM_READ_WRITE,
                                  sw * sh * sizeof(Float4), nullptr, &status);
  cl_kernel decode = a.createKernel(program, "wipReviewDecodeSource", &status);
  cl_kernel resample = a.createKernel(program, "wipReviewResampled", &status);
  if (!decoded || !decode || !resample) {
    if (decoded)
      a.releaseMem(decoded);
    if (decode)
      a.releaseKernel(decode);
    if (resample)
      a.releaseKernel(resample);
    releaseConstants();
    error =
        "OpenCL resample resource creation failed: " + std::to_string(status);
    return RenderStatus::Failed;
  }
  status = a.setKernelArg(decode, 0, sizeof(source), &source);
  status |= a.setKernelArg(decode, 1, sizeof(decoded), &decoded);
  status |= a.setKernelArg(decode, 2, sizeof(paramsBuffer), &paramsBuffer);
  const size_t sourceWork[2] = {sw, sh};
  if (status == CL_SUCCESS)
    status = a.enqueueKernel(queue, decode, 2, nullptr, sourceWork, nullptr, 0,
                             nullptr, nullptr);
  status |= a.setKernelArg(resample, 0, sizeof(decoded), &decoded);
  status |= a.setKernelArg(resample, 1, sizeof(output), &output);
  status |= a.setKernelArg(resample, 2, sizeof(maskBuffer), &maskBuffer);
  status |= a.setKernelArg(resample, 3, sizeof(paramsBuffer), &paramsBuffer);
  status |= a.setKernelArg(resample, 4, sizeof(layerBuffer), &layerBuffer);
  if (status == CL_SUCCESS)
    status = a.enqueueKernel(queue, resample, 2, nullptr, outputWork, nullptr,
                             0, nullptr, nullptr);
  a.releaseKernel(decode);
  a.releaseKernel(resample);
  a.releaseMem(decoded);
  releaseConstants();
  if (status != CL_SUCCESS) {
    error = "OpenCL resample dispatch failed: " + std::to_string(status);
    return RenderStatus::Failed;
  }
  return RenderStatus::Rendered;
}

} // namespace wipreview::gpu
