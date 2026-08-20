#include "probe_core.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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
    {"dci_fit", 4608, 3164, 4096, 2160},
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

void runCase(const RasterCase& raster, DisplayEncoding encoding,
             unsigned int requestedThreads) {
  const std::size_t sourcePixels =
      static_cast<std::size_t>(raster.sourceWidth) * raster.sourceHeight * 4;
  const std::size_t outputPixels =
      static_cast<std::size_t>(raster.outputWidth) * raster.outputHeight * 4;
  std::vector<float> source(sourcePixels);
  std::vector<float> working(outputPixels, 0.0F);
  std::vector<float> output(outputPixels, 0.0F);
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
  const unsigned int threadCount = std::max(
      1U, std::min(requestedThreads,
                   static_cast<unsigned int>(raster.outputHeight)));
  std::vector<wipreview::probe::ManagedRenderStats> threadStats(threadCount);
  std::vector<std::thread> workers;
  workers.reserve(threadCount > 1 ? threadCount : 0);
  std::atomic<bool> renderFailed{false};
  auto renderBand = [&](unsigned int index) {
    const int y1 = raster.outputHeight * static_cast<int>(index) /
        static_cast<int>(threadCount);
    const int y2 = raster.outputHeight * static_cast<int>(index + 1U) /
        static_cast<int>(threadCount);
    try {
      if (!wipreview::probe::renderManagedDisplayFrame(
              sourceView, workingView, {0, y1, raster.outputWidth, y2},
              render, color, &threadStats[index])) {
        renderFailed.store(true, std::memory_order_relaxed);
      }
    } catch (...) {
      renderFailed.store(true, std::memory_order_relaxed);
    }
  };
  if (threadCount == 1) {
    renderBand(0);
  } else {
    for (unsigned int index = 0; index < threadCount; ++index) {
      workers.emplace_back(renderBand, index);
    }
    for (auto& worker : workers) worker.join();
  }
  if (renderFailed.load(std::memory_order_relaxed)) {
    throw std::runtime_error("managed render contract failed");
  }
  const auto renderEnd = Clock::now();
  wipreview::probe::ManagedRenderStats renderStats;
  for (const auto& stats : threadStats) {
    renderStats.decodedRows += stats.decodedRows;
    renderStats.peakCachedRows += stats.peakCachedRows;
    renderStats.peakCacheBytes += stats.peakCacheBytes;
  }

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

  workers.clear();
  renderFailed.store(false, std::memory_order_relaxed);
  auto encodeBand = [&](unsigned int index) {
    const int y1 = raster.outputHeight * static_cast<int>(index) /
        static_cast<int>(threadCount);
    const int y2 = raster.outputHeight * static_cast<int>(index + 1U) /
        static_cast<int>(threadCount);
    try {
      wipreview::probe::encodeManagedDisplayFrame(
          workingView, outputView, {0, y1, raster.outputWidth, y2},
          color, true);
    } catch (...) {
      renderFailed.store(true, std::memory_order_relaxed);
    }
  };
  if (threadCount == 1) {
    encodeBand(0);
  } else {
    for (unsigned int index = 0; index < threadCount; ++index) {
      workers.emplace_back(encodeBand, index);
    }
    for (auto& worker : workers) worker.join();
  }
  if (renderFailed.load(std::memory_order_relaxed)) {
    throw std::runtime_error("managed encode failed");
  }
  const auto encodeEnd = Clock::now();
  const std::uint64_t checksum = quantizedChecksum(output);

  std::cout << std::fixed << std::setprecision(3)
            << "case=" << raster.name
            << " encoding=" << encodingName(encoding)
            << " source=" << raster.sourceWidth << 'x' << raster.sourceHeight
            << " output=" << raster.outputWidth << 'x' << raster.outputHeight
            << " threads=" << threadCount
            << " render_ms=" << milliseconds(renderStart, renderEnd)
            << " overlay_ms=" << milliseconds(renderEnd, overlayEnd)
            << " encode_ms=" << milliseconds(overlayEnd, encodeEnd)
            << " total_ms=" << milliseconds(renderStart, encodeEnd)
            << " decoded_rows=" << renderStats.decodedRows
            << " row_cache_peak_bytes=" << renderStats.peakCacheBytes
            << " checksum=0x" << std::hex << checksum << std::dec << '\n';
}

void runLocalizedIdentityCase(const RasterCase& raster,
                              DisplayEncoding encoding,
                              float blankingOpacity,
                              unsigned int threadCount) {
  const std::size_t pixelCount =
      static_cast<std::size_t>(raster.outputWidth) * raster.outputHeight;
  std::vector<float> source(pixelCount * 4);
  std::vector<float> working(pixelCount * 4);
  std::vector<float> output(pixelCount * 4);
  wipreview::probe::ManagedDirtyRegion dirty(
      {0, 0, raster.outputWidth, raster.outputHeight});
  fillSource(source, raster.sourceWidth, raster.sourceHeight);
  auto sourceView = floatView(source, raster.sourceWidth, raster.sourceHeight);
  auto workingView = floatView(
      working, raster.outputWidth, raster.outputHeight);
  auto outputView = floatView(output, raster.outputWidth, raster.outputHeight);
  const RectI window{0, 0, raster.outputWidth, raster.outputHeight};
  const DisplayConfig color = displayConfig(encoding);
  BlankingOptions blanking;
  blanking.enabled = true;
  blanking.editorialAspect = 2.0;
  blanking.opacity = blankingOpacity;
  const int maskWidth = std::max(32, raster.outputWidth / 8);
  const int maskHeight = std::max(12, raster.outputHeight / 24);
  const auto maskPixels = makeMask(maskWidth, maskHeight);
  const GlyphMaskView mask{
      maskPixels.data(), maskWidth, maskHeight, maskWidth};
  constexpr std::array<TextAnchor, 6> anchors{{
      TextAnchor::TopLeft, TextAnchor::TopCenter, TextAnchor::TopRight,
      TextAnchor::BottomLeft, TextAnchor::BottomCenter,
      TextAnchor::BottomRight,
  }};

  const auto begin = Clock::now();
  if (!wipreview::probe::copyIdentityFrame(
          sourceView, outputView, window, true, true)) {
    throw std::runtime_error("localized identity copy failed");
  }
  for (TextAnchor anchor : anchors) {
    TextOverlayOptions text;
    text.enabled = true;
    text.anchor = anchor;
    text.colour[0] = 1.0F;
    text.colour[1] = 0.72F;
    text.colour[2] = 0.18F;
    wipreview::probe::prepareManagedTextPixels(
        outputView, workingView, dirty, window,
        mask, text, color, true);
  }
  auto blankingBand = [&](unsigned int index) {
    const int y1 = window.y1 +
        (window.y2 - window.y1) * static_cast<int>(index) /
            static_cast<int>(threadCount);
    const int y2 = window.y1 +
        (window.y2 - window.y1) * static_cast<int>(index + 1U) /
            static_cast<int>(threadCount);
    wipreview::probe::compositeManagedIdentityBlankingFused(
        outputView, workingView, dirty,
        {window.x1, y1, window.x2, y2}, blanking, color, true);
  };
  std::vector<std::thread> blankingWorkers;
  if (threadCount == 1) {
    blankingBand(0);
  } else {
    blankingWorkers.reserve(threadCount);
    for (unsigned int index = 0; index < threadCount; ++index) {
      blankingWorkers.emplace_back(blankingBand, index);
    }
    for (auto& worker : blankingWorkers) worker.join();
  }
  for (TextAnchor anchor : anchors) {
    TextOverlayOptions text;
    text.enabled = true;
    text.anchor = anchor;
    text.colour[0] = 1.0F;
    text.colour[1] = 0.72F;
    text.colour[2] = 0.18F;
    wipreview::probe::compositeTextMask(workingView, window, mask, text);
  }
  wipreview::probe::encodeManagedDirtyPixels(
      workingView, outputView, dirty, window,
      color, true);
  const auto end = Clock::now();
  const auto dirtyCount = dirty.count();
  std::cout << std::fixed << std::setprecision(3)
            << "case=" << raster.name
            << " path=localized_identity"
            << " blanking_opacity=" << blankingOpacity
            << " encoding=" << encodingName(encoding)
            << " source=" << raster.sourceWidth << 'x' << raster.sourceHeight
            << " output=" << raster.outputWidth << 'x' << raster.outputHeight
            << " threads=" << threadCount
            << " total_ms=" << milliseconds(begin, end)
            << " dirty_pixels=" << dirtyCount
            << " dirty_percent="
            << 100.0 * static_cast<double>(dirtyCount) /
                   static_cast<double>(pixelCount)
            << " checksum=0x" << std::hex << quantizedChecksum(output)
            << std::dec << '\n';
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
    unsigned int threadCount = 1;
    for (int index = 1; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--case" && index + 1 < argc) {
        caseName = argv[++index];
      } else if (argument == "--encoding" && index + 1 < argc) {
        encodingName = argv[++index];
      } else if (argument == "--threads" && index + 1 < argc) {
        threadCount = static_cast<unsigned int>(std::stoul(argv[++index]));
        if (threadCount == 0) throw std::runtime_error("threads must be positive");
      } else {
        throw std::runtime_error(
            "usage: wipreview_cpu_benchmark [--case equivalence_probe|fullres_to_hd|uhd_identity|dci_fit] "
            "[--encoding rec709|pq|hlg|all] [--threads N]");
      }
    }
    const RasterCase& raster = selectCase(caseName);
    for (DisplayEncoding encoding : selectEncodings(encodingName)) {
      runCase(raster, encoding, threadCount);
      if (raster.sourceWidth == raster.outputWidth &&
          raster.sourceHeight == raster.outputHeight) {
        runLocalizedIdentityCase(raster, encoding, 0.5F, threadCount);
        runLocalizedIdentityCase(raster, encoding, 1.0F, threadCount);
      }
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
