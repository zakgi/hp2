#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace hp2::host {

enum class FontError : std::uint8_t {
  kBadSize,
};

// A decoded LETTRE*.BIN font: 8x8 glyphs for the characters from kFirstCharacter on.
struct Font {
  static constexpr auto kFirstCharacter = std::uint8_t{0x20};
  static constexpr auto kGlyphSize = std::uint16_t{8};
  static constexpr auto kGlyphPixels = std::size_t{kGlyphSize} * kGlyphSize;

  // Color indices (0-15), glyph after glyph, each 8 rows of 8.
  std::vector<std::uint8_t> pixels;

  [[nodiscard]] std::size_t GlyphCount() const { return pixels.size() / kGlyphPixels; }
};

// Decodes a font (docs/formats.md, "Fonts: LETTRE1.BIN, LETTRE2.BIN") the way the text routine
// (0:0be2) reads it: 32 bytes a glyph, for each of its 8 rows one byte of each plane, 0 to 3.
[[nodiscard]] std::expected<Font, FontError> DecodeFont(std::span<const std::uint8_t> file);

}  // namespace hp2::host
