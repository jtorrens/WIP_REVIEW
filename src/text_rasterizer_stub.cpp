#include "text_rasterizer.hpp"

namespace wipreview::text {

GlyphMask rasterizeUTF8(const std::string&, const std::string&,
                        FontStyle, double) noexcept {
  return {};
}

}  // namespace wipreview::text
