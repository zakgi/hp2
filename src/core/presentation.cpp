#include "core/presentation.hpp"

#include <cstdint>
#include <string_view>

namespace hp2 {

void ShowPalette(Screen& screen, PaletteProgram program, std::uint8_t level) {
  for (const auto viewport : {Viewport::kUpper, Viewport::kLower}) {
    auto& palette = screen.Palette(viewport);
    for (const auto& segment : program) {
      if (segment.first_row <= screen.FirstRow(viewport)) {
        palette.Overlay(segment, level);
      }
    }
  }
}

void DrawText(Screen& screen, const BitmapFont& font, std::string_view text, Point origin) {
  for (const auto character : text) {
    const auto glyph = font.GetGlyph(character);
    if (not glyph.pixels.empty()) {
      screen.Blit(glyph, origin);
    }
    origin.x = static_cast<std::int16_t>(origin.x + BitmapFont::kGlyphSize);
  }
}

void PaletteFade::Init() {
  level_ = first_level_;
  ShowPalette(screen_, program_, level_);
  remaining_seconds_ = step_seconds_;
}

bool PaletteFade::Tick(float delta_seconds) {
  remaining_seconds_ -= delta_seconds;
  while (remaining_seconds_ <= 0.0F and level_ != last_level_) {
    level_ = static_cast<std::uint8_t>(level_ < last_level_ ? level_ + 1 : level_ - 1);
    ShowPalette(screen_, program_, level_);
    remaining_seconds_ += step_seconds_;
  }
  return level_ == last_level_ and remaining_seconds_ <= 0.0F;
}

}  // namespace hp2
