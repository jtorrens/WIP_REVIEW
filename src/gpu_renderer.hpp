#pragma once

#include "color_pipeline.hpp"
#include "probe_core.hpp"

#include <string>
#include <vector>

namespace wipreview::gpu {

enum class RenderStatus {
  Rendered,
  Unsupported,
  Failed,
};

struct MaskLayer {
  probe::GlyphMaskView mask{};
  probe::PointI origin{};
  float colour[4] = {1.0F, 1.0F, 1.0F, 1.0F};
  float opacity = 1.0F;
};

struct RenderRequest {
  void *sourceBuffer = nullptr;
  void *outputBuffer = nullptr;
  void *commandQueue = nullptr;
  probe::ImageView sourceFormat{};
  probe::ImageView outputFormat{};
  probe::RectI renderWindow{};
  bool sourcePremultiplied = true;
  bool outputPremultiplied = true;
  probe::RenderOptions renderOptions{};
  probe::BlankingOptions blanking{};
  color::DisplayConfig color{};
  std::vector<MaskLayer> layers;
};

[[nodiscard]] RenderStatus renderMetal(const RenderRequest &request,
                                       std::string &error) noexcept;

[[nodiscard]] RenderStatus renderOpenCL(const RenderRequest &request,
                                        std::string &error) noexcept;

} // namespace wipreview::gpu
