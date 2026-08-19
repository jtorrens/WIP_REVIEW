#include "probe_core.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using wipreview::probe::ImageView;
using wipreview::probe::RectI;

namespace {

void testIntersection() {
  const RectI value = wipreview::probe::intersect({0, 0, 10, 10}, {3, -2, 12, 7});
  assert(value.x1 == 3 && value.y1 == 0 && value.x2 == 10 && value.y2 == 7);
  assert(wipreview::probe::empty({1, 1, 1, 4}));
}

void testCopyAndClear() {
  std::array<std::uint8_t, 16> source{};
  std::array<std::uint8_t, 36> destination{};
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = static_cast<std::uint8_t>(i + 1);
  }
  destination.fill(0xff);

  const ImageView src{reinterpret_cast<std::byte*>(source.data()), {1, 1, 5, 5}, 4, 1};
  const ImageView dst{reinterpret_cast<std::byte*>(destination.data()), {0, 0, 6, 6}, 6, 1};
  wipreview::probe::copyProbeFrame(src, dst, {0, 0, 6, 6});

  assert(destination[0] == 0);
  assert(destination[1 + 1 * 6] == 1);
  assert(destination[4 + 4 * 6] == 16);
  assert(destination[5 + 5 * 6] == 0);
}

void testNegativeRowBytes() {
  std::array<std::uint8_t, 4> source{1, 2, 3, 4};
  std::array<std::uint8_t, 4> destination{};

  const ImageView src{reinterpret_cast<std::byte*>(source.data() + 2), {0, 0, 2, 2}, -2, 1};
  const ImageView dst{reinterpret_cast<std::byte*>(destination.data() + 2), {0, 0, 2, 2}, -2, 1};
  wipreview::probe::copyProbeFrame(src, dst, {0, 0, 2, 2});
  assert(destination == source);
}

}  // namespace

int main() {
  testIntersection();
  testCopyAndClear();
  testNegativeRowBytes();
  return 0;
}

