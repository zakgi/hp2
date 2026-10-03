#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/image_view.hpp"

namespace hp2 {

// An 8x8 font (LETTRE1.BIN, LETTRE2.BIN): one glyph per character from kFirstCharacter on, glyph
// after glyph, colour indices row by row.
struct BitmapFont {
  static constexpr auto kFirstCharacter = std::uint8_t{0x20};
  static constexpr auto kGlyphSize = std::uint16_t{8};
  static constexpr auto kGlyphPixels = std::size_t{kGlyphSize} * kGlyphSize;

  std::span<const std::uint8_t> pixels;

  [[nodiscard]] constexpr std::size_t GlyphCount() const { return pixels.size() / kGlyphPixels; }
  // The glyph of `character`; an empty image when the font has none.
  [[nodiscard]] constexpr ImageView GetGlyph(char character) const {
    const auto code = static_cast<std::uint8_t>(character);
    const auto index = std::size_t{code} - kFirstCharacter;
    const auto present = code >= kFirstCharacter and index < GlyphCount();
    return present ? ImageView{.width = kGlyphSize,
                               .height = kGlyphSize,
                               .pixels = pixels.subspan(index * kGlyphPixels, kGlyphPixels)}
                   : ImageView{};
  }
};

}  // namespace hp2
