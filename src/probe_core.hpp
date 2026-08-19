#pragma once

#include <cstddef>
#include <cstdint>

namespace wipreview::probe {

struct RectI {
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
};

struct ImageView {
  std::byte* data = nullptr;
  RectI bounds{};
  std::ptrdiff_t rowBytes = 0;
  std::size_t pixelBytes = 0;
};

[[nodiscard]] RectI intersect(RectI a, RectI b) noexcept;
[[nodiscard]] bool empty(RectI rect) noexcept;

// Clears the requested destination window, then copies the coordinate-aligned
// source intersection when the two formats have the same pixel size. This is
// deliberately not a resize: P0 must observe host geometry, not hide it.
void copyProbeFrame(const ImageView& source,
                    const ImageView& destination,
                    RectI renderWindow) noexcept;

}  // namespace wipreview::probe

