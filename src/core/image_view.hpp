#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace hp2 {

// Non-owning indexed image. Rows are stored top-down, `width` pixels each, with no padding. The
// pixels are read-only and must outlive every use of the view: a host container, or a flash
// partition on the target, where the views lie over the asset image.
struct ImageView {
  std::uint16_t width{};
  std::uint16_t height{};
  std::span<const std::uint8_t> pixels;

  [[nodiscard]] constexpr std::span<const std::uint8_t> Row(std::uint16_t row) const {
    return pixels.subspan(std::size_t{row} * width, width);
  }
};

// Views are plain values: the generated flash layout builds them in a constexpr function and they
// are copied freely. The pixel blob itself only needs 4-byte alignment in flash.
static_assert(std::is_trivially_copyable_v<ImageView>);

}  // namespace hp2
