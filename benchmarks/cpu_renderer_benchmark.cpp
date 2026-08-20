#include "probe_core.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using wipreview::color::DisplayConfig;
using wipreview::color::DisplayEncoding;
using wipreview::probe::BlankingOptions;
using wipreview::probe::ChannelType;
using wipreview::probe::GlyphMaskView;
using wipreview::probe::ImageView;
using wipreview::probe::PlacementMode;
using wipreview::probe::RectI;
using wipreview::probe::RenderOptions;
using wipreview::probe::ResampleFilter;
using wipreview::probe::TextAnchor;
using wipreview::probe::TextOverlayOptions;

struct RasterCase {
  const char* name;
  int sourceWidth;
  int sourceHeight;
  int outputWidth;
  int outputHeight;
};

constexpr std::array<RasterCase, 4> kCases{{
    {"equivalence_probe", 512, 352, 320, 180},
    {"fullres_to_hd", 4608, 3164, 1920, 1080},
    {"uhd_identity", 3840, 2160, 3840, 2160},
    {"dci_fit", 4096, 2160, 4096, 2160},
}};

ImageView floatView(std::vector<float>& pixels, int width, int height) {
  return {reinterpret_cast<std::byte*>(pixels.data()),
          {0, 0, width, height},
          static_cast<std::ptrdiff_t>(width * 4 * sizeof(float)),
          4 * sizeof(float), 4, ChannelType::Float32};
}

void fillSource(std::vector<float>& pixels, int width, int height) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t index = static_cast<std::size_t>(y * width + x) * 4;
      const float fx = static_cast<float>(x) / std::max(1, width - 1);
      const float fy = static_cast<float>(y) / std::max(1, height - 1);
      const float alpha = 0.35F + 0.65F * static_cast<float>((x + y) % 257) / 256.0F;
      pixels[index + 0] = fx * alpha;
      pixels[index + 1] = fy * alpha;
      pixels[index + 2] = (0.25F + 0.5F * fx * fy) * alpha;
      pixels[index + 3] = alpha;
    }
  }
}

std::vector<std::uint8_t> makeMask(int width, int height) {
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int edge = std::min({x, y, width - 1 - x, height - 1 - y});
      const int stripe = ((x / 7) + (y / 5)) % 2;
      mask[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(
          edge <= 0 ? 0 : (stripe == 0 ? 192 : 255));
    }
  }
  return mask;
}

std::uint64_t quantizedChecksum(const std::vector<float>& pixels) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (float value : pixels) {
    const double finite = std::isfinite(value) ? value : 0.0;
    const auto quantized = static_cast<std::int64_t>(
        std::llround(std::clamp(finite, -16.0, 16.0) * 65536.0));
    const std::uint64_t bits = static_cast<std::uint64_t>(quantized);
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= (bits >> (byte * 8)) & 0xffU;
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

double milliseconds(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

DisplayConfig displayConfig(DisplayEncoding encoding) {
  DisplayConfig config;
  config.encoding = encoding;
  config.peakNits = 1000.0;
  config.graphicsWhiteNits =
      wipreview::color::automaticGraphicsWhiteNits(encoding, config.peakNits);
  return config;
}

const char* encodingName(DisplayEncoding encoding) {
  switch (encoding) {
    case DisplayEncoding::Rec709Gamma24: return "rec709_gamma24";
    case DisplayEncoding::Rec2100PQ: return "rec2100_pq";
    case DisplayEncoding::Rec2100HLG: return "rec2100_hlg";
  }
  return "unknown";
}

void runCase(const RasterCase& raster, DisplayEncoding encoding) {
  const std::size_t sourcePixels =
      static_cast<std::size_t>(raster.sourceWidth) * raster.sourceHeight * 4;
  const std::size_t outputPixels =
      static_cast<std::size_t>(raster.outputWidth) * raster.outputHeight * 4;
  std::vector<float> source(sourcePixels);
  std::vector<float> working(outputPixels, 0.0F);
  std::vector<float> output(outputPixels, 0.0F);
  const bool directIdentityDecode =
      raster.sourceWidth == raster.outputWidth &&
      raster.sourceHeight == raster.outputHeight;
  std::vector<float> decodedSource(
      directIdentityDecode ? 0 : sourcePixels, 0.0F);
  fillSource(source, raster.sourceWidth, raster.sourceHeight);

  auto sourceView = floatView(source, raster.sourceWidth, raster.sourceHeight);
  auto workingView = floatView(working, raster.outputWidth, raster.outputHeight);
  auto outputView = floatView(output, raster.outputWidth, raster.outputHeight);
  const RectI renderWindow{0, 0, raster.outputWidth, raster.outputHeight};

  RenderOptions render;
  render.placement = raster.sourceWidth == raster.outputWidth &&
                             raster.sourceHeight == raster.outputHeight
                         ? PlacementMode::Identity
                         : PlacementMode::Fit;
  render.filter = ResampleFilter::Lanczos3;
  render.sourcePremultiplied = true;
  render.outputPremultiplied = true;
  const DisplayConfig color = displayConfig(encoding);

  BlankingOptions blanking;
  blanking.enabled = true;
  blanking.editorialAspect = 2.0;
  blanking.opacity = 0.5F;
  blanking.outputPremultiplied = true;

  const int maskWidth = std::max(32, raster.outputWidth / 8);
  const int maskHeight = std::max(12, raster.outputHeight / 24);
  const auto maskPixels = makeMask(maskWidth, maskHeight);
  const GlyphMaskView mask{maskPixels.data(), maskWidth, maskHeight, maskWidth};

  const auto renderStart = Clock::now();
  ImageView decodedSourceView;
  if (!directIdentityDecode) {
    decodedSourceView = floatView(
        decodedSource, raster.sourceWidth, raster.sourceHeight);
  }
  if (!wipreview::probe::renderManagedDisplayFrame(
          sourceView, decodedSourceView, workingView, renderWindow,
          render, color)) {
    throw std::runtime_error("managed render scratch contract failed");
  }
  const auto renderEnd = Clock::now();

  wipreview::probe::applyBlanking(workingView, renderWindow, blanking);
  constexpr std::array<TextAnchor, 6> anchors{{
      TextAnchor::TopLeft, TextAnchor::TopCenter, TextAnchor::TopRight,
      TextAnchor::BottomLeft, TextAnchor::BottomCenter, TextAnchor::BottomRight,
  }};
  for (TextAnchor anchor : anchors) {
    TextOverlayOptions text;
    text.enabled = true;
    text.anchor = anchor;
    text.outputPremultiplied = true;
    text.colour[0] = 1.0F;
    text.colour[1] = 0.72F;
    text.colour[2] = 0.18F;
    text.colour[3] = 1.0F;
    wipreview::probe::compositeTextMask(workingView, renderWindow, mask, text);
  }
  const auto overlayEnd = Clock::now();

  wipreview::probe::encodeManagedDisplayFrame(
      workingView, outputView, renderWindow, color, true);
  const auto encodeEnd = Clock::now();
  const std::uint64_t checksum = quantizedChecksum(output);

  std::cout << std::fixed << std::setprecision(3)
            << "case=" << raster.name
            << " encoding=" << encodingName(encoding)
            << " source=" << raster.sourceWidth << 'x' << raster.sourceHeight
            << " output=" << raster.outputWidth << 'x' << raster.outputHeight
            << " render_ms=" << milliseconds(renderStart, renderEnd)
            << " overlay_ms=" << milliseconds(renderEnd, overlayEnd)
            << " encode_ms=" << milliseconds(overlayEnd, encodeEnd)
            << " total_ms=" << milliseconds(renderStart, encodeEnd)
            << " checksum=0x" << std::hex << checksum << std::dec << '\n';
}

const RasterCase& selectCase(const std::string& name) {
  const auto found = std::find_if(kCases.begin(), kCases.end(),
      [&](const RasterCase& raster) { return raster.name == name; });
  if (found == kCases.end()) throw std::runtime_error("unknown case: " + name);
  return *found;
}

std::vector<DisplayEncoding> selectEncodings(const std::string& name) {
  if (name == "rec709") return {DisplayEncoding::Rec709Gamma24};
  if (name == "pq") return {DisplayEncoding::Rec2100PQ};
  if (name == "hlg") return {DisplayEncoding::Rec2100HLG};
  if (name == "all") return {DisplayEncoding::Rec709Gamma24,
                              DisplayEncoding::Rec2100PQ,
                              DisplayEncoding::Rec2100HLG};
  throw std::runtime_error("unknown encoding: " + name);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string caseName = "fullres_to_hd";
    std::string encodingName = "all";
    for (int index = 1; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--case" && index + 1 < argc) {
        caseName = argv[++index];
      } else if (argument == "--encoding" && index + 1 < argc) {
        encodingName = argv[++index];
      } else {
        throw std::runtime_error(
            "usage: wipreview_cpu_benchmark [--case equivalence_probe|fullres_to_hd|uhd_identity|dci_fit] "
            "[--encoding rec709|pq|hlg|all]");
      }
    }
    for (DisplayEncoding encoding : selectEncodings(encodingName)) {
      runCase(selectCase(caseName), encoding);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
