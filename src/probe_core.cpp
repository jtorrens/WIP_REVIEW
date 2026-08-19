#include "probe_core.hpp"

#include <algorithm>
#include <cstring>

namespace wipreview::probe {

RectI intersect(RectI a, RectI b) noexcept {
  return {std::max(a.x1, b.x1), std::max(a.y1, b.y1),
          std::min(a.x2, b.x2), std::min(a.y2, b.y2)};
}

bool empty(RectI rect) noexcept {
  return rect.x1 >= rect.x2 || rect.y1 >= rect.y2;
}

namespace {

std::byte* pixelAddress(const ImageView& image, int x, int y) noexcept {
  return image.data + static_cast<std::ptrdiff_t>(y - image.bounds.y1) * image.rowBytes
       + static_cast<std::ptrdiff_t>(x - image.bounds.x1)
             * static_cast<std::ptrdiff_t>(image.pixelBytes);
}

}  // namespace

void copyProbeFrame(const ImageView& source,
                    const ImageView& destination,
                    RectI renderWindow) noexcept {
  if (!destination.data || destination.pixelBytes == 0) {
    return;
  }

  const RectI writable = intersect(renderWindow, destination.bounds);
  if (empty(writable)) {
    return;
  }

  const auto writableBytes = static_cast<std::size_t>(writable.x2 - writable.x1)
                           * destination.pixelBytes;
  for (int y = writable.y1; y < writable.y2; ++y) {
    std::memset(pixelAddress(destination, writable.x1, y), 0, writableBytes);
  }

  if (!source.data || source.pixelBytes != destination.pixelBytes) {
    return;
  }

  const RectI copyArea = intersect(writable, source.bounds);
  if (empty(copyArea)) {
    return;
  }

  const auto copyBytes = static_cast<std::size_t>(copyArea.x2 - copyArea.x1)
                       * destination.pixelBytes;
  for (int y = copyArea.y1; y < copyArea.y2; ++y) {
    std::memcpy(pixelAddress(destination, copyArea.x1, y),
                pixelAddress(source, copyArea.x1, y), copyBytes);
  }
}

}  // namespace wipreview::probe

