#pragma once

// Pieces the screens share: where text goes, and the actions that time them
// (core/action_stack.hpp).

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "core/bitmap_font.hpp"
#include "core/palette.hpp"
#include "core/screen.hpp"

namespace hp2 {

// How long a fade in or out takes.
inline constexpr auto kFadeSeconds = 0.5F;

// The top-left pixel of character cell (column, row): the original places text on an 8 x 8 grid.
[[nodiscard]] constexpr Point TextCell(std::int16_t column, std::int16_t row) {
  return Point{.x = static_cast<std::int16_t>(column * BitmapFont::kGlyphSize),
               .y = static_cast<std::int16_t>(row * BitmapFont::kGlyphSize)};
}

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

// Moves the screen's palette from `start` to `end` brightness over `seconds`, eased so it starts and
// ends gently: fade in 0 to 1, fade out 1 to 0. Full brightness is the palette as it is when the fade
// is made, and the fade shows `start` from then on, so a fade in is black before its first frame.
class Fade {
 public:
  Fade(Screen& screen, float start, float end, float seconds)
      : palette_(screen.Palette(Viewport::kUpper)), colors_(palette_), start_(start), end_(end), seconds_(seconds) {
    Show(start_);
  }

  void Init() { elapsed_ = 0.0F; }
  bool Tick(float delta_seconds) {
    elapsed_ = std::min(elapsed_ + delta_seconds, seconds_);
    const auto progress = seconds_ > 0.0F ? elapsed_ / seconds_ : 1.0F;
    const auto eased = progress * progress * (3.0F - (2.0F * progress));
    Show(start_ + ((end_ - start_) * eased));
    return elapsed_ >= seconds_;
  }

 private:
  // Sets every entry to its full-brightness color scaled by `brightness`.
  void Show(float brightness) {
    for (auto index = std::size_t{0}; index < ScreenPalette::kColorCount; ++index) {
      const auto entry = static_cast<std::uint8_t>(index);
      palette_.SetColor(entry, ScaleColor(colors_.Color(entry), brightness));
    }
  }

  ScreenPalette& palette_;
  ScreenPalette colors_;
  float start_;
  float end_;
  float seconds_;
  float elapsed_{};
};

}  // namespace hp2
