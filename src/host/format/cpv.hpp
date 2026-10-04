#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace hp2::host {

enum class CpvError : std::uint8_t {
  kTooShort,
  kBadMagic,
  kZeroRun,
  kTruncatedStream,
};

// A decoded .CPV picture (docs/formats.md, "Pictures: .CPV").
struct CpvPicture {
  static constexpr std::uint16_t kWidth = 320;
  static constexpr std::uint16_t kHeight = 200;

  // The header palette as stored: Atari ST colors, three bits per gun.
  std::array<std::uint16_t, 16> palette_st{};
  // One color index (0-15) per pixel, top-down rows.
  std::vector<std::uint8_t> pixels;
  // Bytes of the file the decoder read; it stops as soon as the four planes are full.
  std::size_t consumed{};
};

// Decodes like DecodeCPV (0:0f6c): word 0x1234, 16 palette words, then byte runs (c < 0x80: the
// next byte c times; c >= 0x80: c & 0x7f literal bytes) filling each bitplane column by column
// (200 rows per byte column, 40 columns), plane 0 to 3. The planes are converted to color indices.
[[nodiscard]] std::expected<CpvPicture, CpvError> DecodeCpv(std::span<const std::uint8_t> file);

}  // namespace hp2::host
