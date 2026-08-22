#include "gpu_renderer.hpp"

#include <cuda_fp16.h>
#include <cuda_runtime_api.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace wipreview::gpu {
namespace {

struct alignas(16) Params {
  int4 sourceBounds{}, outputBounds{}, renderWindow{};
  int sourceRowBytes = 0, outputRowBytes = 0, sourcePixelBytes = 0,
      outputPixelBytes = 0, channelType = 0, sourcePremultiplied = 1,
      outputPremultiplied = 1, encoding = 0;
  float graphicsWhite = 100.0F, peakNits = 1000.0F;
  int filter = 0;
  float4 transform{}, canvas{};
  int blankingEnabled = 0;
  float blankingOpacity = 1.0F;
  float2 reserved0{};
  float4 blankingColour{}, aperture{};
  int layerCount = 0;
  int3 reserved1{};
};
struct alignas(16) Layer { int4 geometry{}, mask{}; float4 colour{}, compositing{}; };
static_assert(sizeof(Layer) == 64);

__device__ float clamp01(float value) { return fminf(fmaxf(value, 0.0F), 1.0F); }
__device__ float readChannel(const unsigned char* pixel, int c, int type) {
  if (type == 0) return static_cast<float>(pixel[c]) / 255.0F;
  if (type == 1) return static_cast<float>(reinterpret_cast<const unsigned short*>(pixel)[c]) / 65535.0F;
  if (type == 2) return __half2float(reinterpret_cast<const __half*>(pixel)[c]);
  return reinterpret_cast<const float*>(pixel)[c];
}
__device__ void writeChannel(unsigned char* pixel, int c, int type, float value) {
  if (type == 0) pixel[c] = static_cast<unsigned char>(__float2int_rn(clamp01(value) * 255.0F));
  else if (type == 1) reinterpret_cast<unsigned short*>(pixel)[c] = static_cast<unsigned short>(__float2int_rn(clamp01(value) * 65535.0F));
  else if (type == 2) reinterpret_cast<__half*>(pixel)[c] = __float2half(value);
  else reinterpret_cast<float*>(pixel)[c] = value;
}
__device__ float pqToNits(float x) { const float m1=2610.F/16384.F,m2=2523.F/32.F,c1=3424.F/4096.F,c2=2413.F/128.F,c3=2392.F/128.F; const float v=powf(clamp01(x),1.F/m2); const float d=c2-c3*v; return d<=0.F?10000.F:10000.F*powf(fmaxf(v-c1,0.F)/d,1.F/m1); }
__device__ float nitsToPq(float x) { const float m1=2610.F/16384.F,m2=2523.F/32.F,c1=3424.F/4096.F,c2=2413.F/128.F,c3=2392.F/128.F; const float v=powf(clamp01(x/10000.F),m1); return powf((c1+c2*v)/(1.F+c3*v),m2); }
__device__ float hlgInverse(float x) { const float a=.17883277F,b=.28466892F,c=.55991073F; x=fmaxf(x,0.F); return x<=.5F?x*x/3.F:(expf((x-c)/a)+b)/12.F; }
__device__ float hlgForward(float x) { const float a=.17883277F,b=.28466892F,c=.55991073F; x=fmaxf(x,0.F); return x<=1.F/12.F?sqrtf(3.F*x):a*logf(12.F*x-b)+c; }
__device__ float3 decode(float3 v, const Params& p) { if (p.encoding == 0) return make_float3(copysignf(powf(fabsf(v.x),2.4F),v.x),copysignf(powf(fabsf(v.y),2.4F),v.y),copysignf(powf(fabsf(v.z),2.4F),v.z)); if (p.encoding == 1) return make_float3(pqToNits(v.x)/p.graphicsWhite,pqToNits(v.y)/p.graphicsWhite,pqToNits(v.z)/p.graphicsWhite); float3 s=make_float3(hlgInverse(v.x),hlgInverse(v.y),hlgInverse(v.z)); float l=fmaxf(.2627F*s.x+.6780F*s.y+.0593F*s.z,0.F),g=1.2F+.42F*log10f(p.peakNits/1000.F),q=p.peakNits*(l>0.F?powf(l,g-1.F):0.F)/p.graphicsWhite; return make_float3(s.x*q,s.y*q,s.z*q); }
__device__ float3 encode(float3 v, const Params& p) { if (p.encoding == 0) return make_float3(copysignf(powf(fabsf(v.x),1.F/2.4F),v.x),copysignf(powf(fabsf(v.y),1.F/2.4F),v.y),copysignf(powf(fabsf(v.z),1.F/2.4F),v.z)); if (p.encoding == 1) return make_float3(nitsToPq(v.x*p.graphicsWhite),nitsToPq(v.y*p.graphicsWhite),nitsToPq(v.z*p.graphicsWhite)); float3 n=make_float3(v.x*p.graphicsWhite,v.y*p.graphicsWhite,v.z*p.graphicsWhite); n.x=fmaxf(n.x,0.F); n.y=fmaxf(n.y,0.F); n.z=fmaxf(n.z,0.F); float l=fmaxf(.2627F*n.x+.6780F*n.y+.0593F*n.z,0.F),g=1.2F+.42F*log10f(p.peakNits/1000.F),sl=l>0.F?powf(l/p.peakNits,1.F/g):0.F,scale=sl>0.F?p.peakNits*powf(sl,g-1.F):1.F; return make_float3(hlgForward(n.x/scale),hlgForward(n.y/scale),hlgForward(n.z/scale)); }
__device__ float4 over(float4 b,float3 c,float a) { return make_float4(c.x*a+b.x*(1.F-a),c.y*a+b.y*(1.F-a),c.z*a+b.z*(1.F-a),a+b.w*(1.F-a)); }
__device__ float filter(float d,int f) { float x=fabsf(d); if(f==0)return x<1.F?1.F-x:0.F; if(f==1){if(x<1.F)return 1.5F*x*x*x-2.5F*x*x+1.F;if(x<2.F)return -.5F*x*x*x+2.5F*x*x-4.F*x+2.F;return 0.F;} if(x>=3.F)return 0.F; if(x<1.e-6F)return 1.F; const float p=3.141592653589793F,px=p*x; return (sinf(px)/px)*(sinf(px/3.F)/(px/3.F)); }
__device__ float4 layersAt(float4 pixel,int x,int y,const unsigned char* masks,const Params& p,const Layer* layers) { if(p.blankingEnabled){bool h=p.aperture.y>p.outputBounds.y||p.aperture.w<p.outputBounds.w;float o=h?fmaxf(0.F,fminf(static_cast<float>(y+1),p.aperture.w)-fmaxf(static_cast<float>(y),p.aperture.y)):fmaxf(0.F,fminf(static_cast<float>(x+1),p.aperture.z)-fmaxf(static_cast<float>(x),p.aperture.x));float a=clamp01((1.F-o)*p.blankingOpacity*p.blankingColour.w);pixel=over(pixel,make_float3(p.blankingColour.x,p.blankingColour.y,p.blankingColour.z),a);} for(int i=0;i<p.layerCount;++i){const Layer l=layers[i];int lx=x-l.geometry.x,ly=y-l.geometry.y;if(lx<0||ly<0||lx>=l.geometry.z||ly>=l.geometry.w)continue;float a=clamp01((static_cast<float>(masks[l.mask.x+ly*l.geometry.z+lx])/255.F)*l.compositing.x*l.colour.w);pixel=over(pixel,make_float3(l.colour.x,l.colour.y,l.colour.z),a);}return pixel; }
__device__ void writePixel(unsigned char* output,float4 pixel,const Params& p) { float a=pixel.w; float3 e=encode(a>1.e-8F?make_float3(pixel.x/a,pixel.y/a,pixel.z/a):make_float3(0.F,0.F,0.F),p); if(p.outputPremultiplied)e=make_float3(e.x*a,e.y*a,e.z*a); writeChannel(output,0,p.channelType,e.x);writeChannel(output,1,p.channelType,e.y);writeChannel(output,2,p.channelType,e.z);writeChannel(output,3,p.channelType,a); }

__global__ void identity(const unsigned char* source,unsigned char* output,const unsigned char* masks,const Params* params,const Layer* layers) { const Params p=*params;const int x=blockIdx.x*blockDim.x+threadIdx.x+p.renderWindow.x,y=blockIdx.y*blockDim.y+threadIdx.y+p.renderWindow.y;if(x>=p.renderWindow.z||y>=p.renderWindow.w)return;const auto* s=source+(y-p.sourceBounds.y)*p.sourceRowBytes+(x-p.sourceBounds.x)*p.sourcePixelBytes;auto* o=output+(y-p.outputBounds.y)*p.outputRowBytes+(x-p.outputBounds.x)*p.outputPixelBytes;float a=readChannel(s,3,p.channelType);float3 e=make_float3(readChannel(s,0,p.channelType),readChannel(s,1,p.channelType),readChannel(s,2,p.channelType));if(p.sourcePremultiplied&&a>1.e-8F)e=make_float3(e.x/a,e.y/a,e.z/a);float3 v=decode(e,p);writePixel(o,layersAt(make_float4(v.x*a,v.y*a,v.z*a,a),x,y,masks,p,layers),p); }
__global__ void decodeSource(const unsigned char* source,float4* decoded,const Params* params) { const Params p=*params;int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y,w=p.sourceBounds.z-p.sourceBounds.x,h=p.sourceBounds.w-p.sourceBounds.y;if(x>=w||y>=h)return;const auto* s=source+y*p.sourceRowBytes+x*p.sourcePixelBytes;float a=readChannel(s,3,p.channelType);float3 e=make_float3(readChannel(s,0,p.channelType),readChannel(s,1,p.channelType),readChannel(s,2,p.channelType));if(p.sourcePremultiplied&&a>1.e-8F)e=make_float3(e.x/a,e.y/a,e.z/a);float3 v=decode(e,p);decoded[y*w+x]=make_float4(v.x*a,v.y*a,v.z*a,a); }
__global__ void resample(const float4* decoded,unsigned char* output,const unsigned char* masks,const Params* params,const Layer* layers) { const Params p=*params;int x=blockIdx.x*blockDim.x+threadIdx.x+p.renderWindow.x,y=blockIdx.y*blockDim.y+threadIdx.y+p.renderWindow.y;if(x>=p.renderWindow.z||y>=p.renderWindow.w)return;auto* o=output+(y-p.outputBounds.y)*p.outputRowBytes+(x-p.outputBounds.x)*p.outputPixelBytes;float ox=.5F*(p.outputBounds.x+p.outputBounds.z),oy=.5F*(p.outputBounds.y+p.outputBounds.w),cx=p.transform.z+(x+.5F-ox)/p.transform.x,cy=p.transform.w+(y+.5F-oy)/p.transform.y;float4 value=p.canvas;if(cx>=p.sourceBounds.x&&cx<p.sourceBounds.z&&cy>=p.sourceBounds.y&&cy<p.sourceBounds.w){float r=p.filter==0?1.F:(p.filter==1?2.F:3.F),fx=fmaxf(1.F,1.F/fabsf(p.transform.x)),fy=fmaxf(1.F,1.F/fabsf(p.transform.y)),sx=cx-.5F,sy=cy-.5F;float4 sum=make_float4(0,0,0,0);float ws=0;int width=p.sourceBounds.z-p.sourceBounds.x;for(int yy=ceilf(sy-r*fy);yy<=floorf(sy+r*fy);++yy){float wy=filter((sy-yy)/fy,p.filter);if(!wy)continue;int ay=max(p.sourceBounds.y,min(yy,p.sourceBounds.w-1));for(int xx=ceilf(sx-r*fx);xx<=floorf(sx+r*fx);++xx){float wx=filter((sx-xx)/fx,p.filter);if(!wx)continue;int ax=max(p.sourceBounds.x,min(xx,p.sourceBounds.z-1));float q=wx*wy;float4 v=decoded[(ay-p.sourceBounds.y)*width+ax-p.sourceBounds.x];sum=make_float4(sum.x+v.x*q,sum.y+v.y*q,sum.z+v.z*q,sum.w+v.w*q);ws+=q;}}value=fabsf(ws)>1.e-12F?make_float4(sum.x/ws,sum.y/ws,sum.z/ws,sum.w/ws):make_float4(0,0,0,0);}writePixel(o,layersAt(value,x,y,masks,p,layers),p); }

int channelTypeIndex(probe::ChannelType t) noexcept { switch(t){case probe::ChannelType::UInt8:return 0;case probe::ChannelType::UInt16:return 1;case probe::ChannelType::Half:return 2;case probe::ChannelType::Float32:return 3;}return -1; }

} // namespace

RenderStatus renderCUDA(const RenderRequest& request, std::string& error) noexcept {
  error.clear();
  if(!request.sourceBuffer||!request.outputBuffer||!request.commandQueue||request.sourceFormat.channels!=4||request.outputFormat.channels!=4||request.sourceFormat.channelType!=request.outputFormat.channelType||request.sourceFormat.pixelBytes!=request.outputFormat.pixelBytes||request.sourceFormat.rowBytes<=0||request.outputFormat.rowBytes<=0){error="CUDA render handles or RGBA formats are incomplete";return RenderStatus::Unsupported;}
  std::vector<unsigned char> masks;std::vector<Layer> layers;for(const auto& sourceLayer:request.layers){if(!sourceLayer.mask.data||sourceLayer.mask.width<=0||sourceLayer.mask.height<=0||sourceLayer.mask.rowBytes<sourceLayer.mask.width)continue;Layer layer{};layer.geometry=make_int4(sourceLayer.origin.x,sourceLayer.origin.y,sourceLayer.mask.width,sourceLayer.mask.height);layer.mask.x=static_cast<int>(masks.size());layer.colour=make_float4(sourceLayer.colour[0],sourceLayer.colour[1],sourceLayer.colour[2],sourceLayer.colour[3]);layer.compositing.x=sourceLayer.opacity;for(int y=0;y<sourceLayer.mask.height;++y){const auto* row=sourceLayer.mask.data+y*sourceLayer.mask.rowBytes;masks.insert(masks.end(),row,row+sourceLayer.mask.width);}layers.push_back(layer);}if(masks.empty())masks.push_back(0);if(layers.empty())layers.emplace_back();
  Params p{};const auto& sb=request.sourceFormat.bounds;const auto& ob=request.outputFormat.bounds;const auto& rw=request.renderWindow;p.sourceBounds=make_int4(sb.x1,sb.y1,sb.x2,sb.y2);p.outputBounds=make_int4(ob.x1,ob.y1,ob.x2,ob.y2);p.renderWindow=make_int4(rw.x1,rw.y1,rw.x2,rw.y2);p.sourceRowBytes=static_cast<int>(request.sourceFormat.rowBytes);p.outputRowBytes=static_cast<int>(request.outputFormat.rowBytes);p.sourcePixelBytes=static_cast<int>(request.sourceFormat.pixelBytes);p.outputPixelBytes=static_cast<int>(request.outputFormat.pixelBytes);p.channelType=channelTypeIndex(request.sourceFormat.channelType);p.sourcePremultiplied=request.sourcePremultiplied;p.outputPremultiplied=request.outputPremultiplied;p.encoding=static_cast<int>(request.color.encoding);p.graphicsWhite=static_cast<float>(request.color.graphicsWhiteNits);p.peakNits=static_cast<float>(request.color.peakNits);p.filter=static_cast<int>(request.renderOptions.filter);const auto t=probe::computePlacement(sb,ob,request.renderOptions);p.transform=make_float4(t.scaleX,t.scaleY,t.sourceCenterX,t.sourceCenterY);float ca=request.renderOptions.canvas[3];p.canvas=make_float4(request.renderOptions.canvas[0]*ca,request.renderOptions.canvas[1]*ca,request.renderOptions.canvas[2]*ca,ca);p.blankingEnabled=request.blanking.enabled;p.blankingOpacity=request.blanking.opacity;p.blankingColour=make_float4(request.blanking.colour[0],request.blanking.colour[1],request.blanking.colour[2],request.blanking.colour[3]);const auto ap=probe::computeBlankingAperture(ob,request.blanking);p.aperture=make_float4(ap.x1,ap.y1,ap.x2,ap.y2);p.layerCount=static_cast<int>(layers.size());
  const auto stream=reinterpret_cast<cudaStream_t>(request.commandQueue);unsigned char* dm=nullptr;Params* dp=nullptr;Layer* dl=nullptr;float4* decoded=nullptr;cudaError_t s=cudaMallocAsync(&dm,masks.size(),stream);if(s==cudaSuccess)s=cudaMallocAsync(&dp,sizeof(p),stream);if(s==cudaSuccess)s=cudaMallocAsync(&dl,layers.size()*sizeof(Layer),stream);if(s==cudaSuccess)s=cudaMemcpyAsync(dm,masks.data(),masks.size(),cudaMemcpyHostToDevice,stream);if(s==cudaSuccess)s=cudaMemcpyAsync(dp,&p,sizeof(p),cudaMemcpyHostToDevice,stream);if(s==cudaSuccess)s=cudaMemcpyAsync(dl,layers.data(),layers.size()*sizeof(Layer),cudaMemcpyHostToDevice,stream);dim3 block(16,16),outGrid((rw.x2-rw.x1+15)/16,(rw.y2-rw.y1+15)/16);bool same=probe::isEffectivelyIdentityPlacement(sb,ob,request.renderOptions);if(s==cudaSuccess&&same){identity<<<outGrid,block,0,stream>>>(static_cast<const unsigned char*>(request.sourceBuffer),static_cast<unsigned char*>(request.outputBuffer),dm,dp,dl);s=cudaGetLastError();}if(s==cudaSuccess&&!same){size_t sw=static_cast<size_t>(std::max(0,sb.x2-sb.x1)),sh=static_cast<size_t>(std::max(0,sb.y2-sb.y1));if(!sw||!sh||sw>std::numeric_limits<size_t>::max()/sh/sizeof(float4))s=cudaErrorInvalidValue;else{s=cudaMallocAsync(&decoded,sw*sh*sizeof(float4),stream);if(s==cudaSuccess){dim3 sourceGrid((sw+15)/16,(sh+15)/16);decodeSource<<<sourceGrid,block,0,stream>>>(static_cast<const unsigned char*>(request.sourceBuffer),decoded,dp);s=cudaGetLastError();}if(s==cudaSuccess){resample<<<outGrid,block,0,stream>>>(decoded,static_cast<unsigned char*>(request.outputBuffer),dm,dp,dl);s=cudaGetLastError();}}}if(decoded)cudaFreeAsync(decoded,stream);if(dm)cudaFreeAsync(dm,stream);if(dp)cudaFreeAsync(dp,stream);if(dl)cudaFreeAsync(dl,stream);if(s!=cudaSuccess){error=std::string("CUDA dispatch failed: ")+cudaGetErrorString(s);return RenderStatus::Failed;}return RenderStatus::Rendered;
}

} // namespace wipreview::gpu
