#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/image_view.hpp"

namespace hp2 {

// One image of a sprite bank: where its colour indices lie in the bank's pixels, its size, and
// the hotspot BlitBob (1:001c) subtracts from the position when asked to.
struct SpriteRange {
  std::uint32_t offset{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::int16_t origin_x{};
  std::int16_t origin_y{};

  friend constexpr bool operator==(const SpriteRange&, const SpriteRange&) = default;
};

// A bank of images (an .IMG file), numbered from 0; the original numbers them from 1. Colour index
// 0 is transparent when an image is drawn masked.
struct SpriteBank {
  std::span<const std::uint8_t> pixels;
  std::span<const SpriteRange> sprites;

  [[nodiscard]] constexpr ImageView GetImage(std::size_t index) const {
    const auto& sprite = sprites[index];
    return ImageView{.width = sprite.width,
                     .height = sprite.height,
                     .pixels = pixels.subspan(sprite.offset, std::size_t{sprite.width} * sprite.height)};
  }
};

}  // namespace hp2
