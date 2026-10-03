#pragma once

// Pieces the screens share: showing a palette list at a fade level, and the actions that time
// them (core/action_stack.hpp).

#include <cstdint>
#include <string_view>

#include "core/bitmap_font.hpp"
#include "core/palette.hpp"
#include "core/screen.hpp"

namespace hp2 {

inline constexpr auto kDarkest = static_cast<std::uint8_t>(kFadeLevels - 1);
inline constexpr auto kFullBrightness = std::uint8_t{0};

// Shows `program` at fade `level`: each viewport takes the segments in effect on its first row.
// Segments starting inside a viewport would need palette entries of their own; the screens' lists
// have none.
void ShowPalette(Screen& screen, PaletteProgram program, std::uint8_t level);

// Calls `draw(origin)` once per viewport with `origin`, given in screen coordinates, moved into the
// viewport's; each viewport clips its share.
template <typename Draw>
void DrawAcrossViewports(Screen& screen, Point origin, Draw draw) {
  for (const auto viewport : {Viewport::kUpper, Viewport::kLower}) {
    if (screen.Rows(viewport) > 0) {
      screen.SetViewport(viewport);
      draw(Point{.x = origin.x, .y = static_cast<std::int16_t>(origin.y - screen.FirstRow(viewport))});
    }
  }
}

// The top-left pixel of character cell (column, row): the original places text on an 8 x 8 grid.
[[nodiscard]] constexpr Point TextCell(std::int16_t column, std::int16_t row) {
  return Point{.x = static_cast<std::int16_t>(column * BitmapFont::kGlyphSize),
               .y = static_cast<std::int16_t>(row * BitmapFont::kGlyphSize)};
}

// Draws `text` in `font` from `origin` on, in the selected viewport, glyphs opaque (DrawString,
// 0:0c9c); a character the font lacks is skipped but takes its place.
void DrawText(Screen& screen, const BitmapFont& font, std::string_view text, Point origin);

// Leaves the screen as it is for a while.
class Hold {
 public:
  explicit Hold(float seconds) : seconds_(seconds) {}

  void Init() { remaining_seconds_ = seconds_; }
  bool Tick(float delta_seconds) {
    remaining_seconds_ -= delta_seconds;
    return remaining_seconds_ <= 0.0F;
  }

 private:
  float seconds_;
  float remaining_seconds_{};
};

// FadePaletteList (0:0d06) stepped from `first_level` to `last_level`, one level every
// `step_seconds`, the first level shown at once and a step's wait after the last.
class PaletteFade {
 public:
  PaletteFade(Screen& screen, PaletteProgram program, std::uint8_t first_level, std::uint8_t last_level,
              float step_seconds)
      : screen_(screen),
        program_(program),
        first_level_(first_level),
        last_level_(last_level),
        step_seconds_(step_seconds) {}

  void Init();
  bool Tick(float delta_seconds);

 private:
  Screen& screen_;
  PaletteProgram program_;
  std::uint8_t first_level_;
  std::uint8_t last_level_;
  float step_seconds_;
  std::uint8_t level_{};
  float remaining_seconds_{};
};

}  // namespace hp2
