#pragma once

#include <cstdint>

#include "core/palette.hpp"

namespace hp2::host {

// How an original palette stores its colours: Amiga colour registers, 4 bits per channel, or Atari
// ST colours, 3 bits per channel (the pictures' headers and the title's palette list).
enum class ColorFormat : std::uint8_t { kAmiga, kAtariSt };

// An Amiga colour word, 0x0RGB with 4 bits per channel: each channel times 17, so 0xf is 0xff.
[[nodiscard]] constexpr Rgb FromAmiga(std::uint16_t color) {
  constexpr auto kMask = std::uint16_t{0xf};
  constexpr auto kScale = 17;
  const auto channel = [color](int shift) {
    return static_cast<std::uint8_t>(((color >> shift) & kMask) * kScale);
  };
  return Rgb{.red = channel(8), .green = channel(4), .blue = channel(0)};
}

// An Atari ST colour word, 0x0RGB with 3 bits per channel: each channel scaled to 0..255, rounded.
[[nodiscard]] constexpr Rgb FromAtariSt(std::uint16_t color) {
  constexpr auto kMask = std::uint16_t{0x7};
  constexpr auto kMax = 7;
  constexpr auto kFull = 255;
  const auto channel = [color](int shift) {
    return static_cast<std::uint8_t>(((((color >> shift) & kMask) * kFull) + (kMax / 2)) / kMax);
  };
  return Rgb{.red = channel(8), .green = channel(4), .blue = channel(0)};
}

[[nodiscard]] constexpr Rgb FromColorWord(std::uint16_t color, ColorFormat format) {
  return format == ColorFormat::kAtariSt ? FromAtariSt(color) : FromAmiga(color);
}

}  // namespace hp2::host
