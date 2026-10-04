#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace hp2::host {

enum class BobBankError : std::uint8_t {
  kTooShort,
  kImageOutOfBank,
  kTooFewTargetPlanes,
};

// One image of a bob bank (docs/formats.md, "Bob banks: .IMG"), as color indices.
struct BobImage {
  std::uint16_t width{};
  std::uint16_t height{};
  // Subtracted from the position when BlitBob's hotspot argument is set.
  std::int16_t origin_x{};
  std::int16_t origin_y{};
  // One color index (0-15) per pixel, top-down rows; 0 is transparent when drawn masked.
  std::vector<std::uint8_t> pixels;
};

// Decodes a bank the way BlitBob (1:001c) reads it: word count, word offsets[count] from the
// bank start, then per image {word flags, word width in words, word height, word origin x, word
// origin y, plane data}. Flags bits 0-3 count the stored planes, bits 8-11 choose the screen planes
// that receive them, the i-th stored plane going to the i-th chosen plane.
[[nodiscard]] std::expected<std::vector<BobImage>, BobBankError> DecodeBobBank(std::span<const std::uint8_t> file);

}  // namespace hp2::host
