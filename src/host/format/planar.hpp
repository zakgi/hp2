#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace hp2::host {

// Bitplane deinterleaving. One byte of a bitplane holds 8 pixels, leftmost in bit 7; a colour
// index is built from one bit of each plane. The table turns a nibble into 4 bytes of 0 or 1,
// leftmost pixel first, so that a plane byte deinterleaves with two lookups.
consteval std::array<std::uint32_t, 16> MakeDeinterleaveTable() {
  auto table = std::array<std::uint32_t, 16>{};
  for (auto nibble = std::size_t{0}; nibble < table.size(); ++nibble) {
    auto pixels = std::array<std::uint8_t, 4>{};
    for (auto pixel = std::size_t{0}; pixel < pixels.size(); ++pixel) {
      pixels[pixel] = static_cast<std::uint8_t>((nibble >> (3 - pixel)) & 1U);
    }
    table[nibble] = std::bit_cast<std::uint32_t>(pixels);
  }
  return table;
}

inline constexpr auto kDeinterleaveTable = MakeDeinterleaveTable();

// The 8 pixels of `planar_byte`, each 1 << `shift` where its bit is set: the contribution of
// bitplane `shift` to the colour indices.
[[nodiscard]] constexpr std::array<std::uint8_t, 8> DeinterleaveShift(std::uint8_t planar_byte,
                                                                      std::uint8_t shift = 0) {
  const auto halves = std::array<std::uint32_t, 2>{kDeinterleaveTable[planar_byte >> 4] << shift,
                                                   kDeinterleaveTable[planar_byte & 0xfU] << shift};
  return std::bit_cast<std::array<std::uint8_t, 8>>(halves);
}

// The 8 pixels of `planar_byte`, each `mask` where its bit is set: for a stored plane that lands
// on screen plane(s) given as an index mask.
[[nodiscard]] constexpr std::array<std::uint8_t, 8> DeinterleaveMask(std::uint8_t planar_byte, std::uint8_t mask) {
  const auto halves = std::array<std::uint32_t, 2>{kDeinterleaveTable[planar_byte >> 4] * mask,
                                                   kDeinterleaveTable[planar_byte & 0xfU] * mask};
  return std::bit_cast<std::array<std::uint8_t, 8>>(halves);
}

}  // namespace hp2::host
