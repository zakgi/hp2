#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "core/bitmap_font.hpp"
#include "core/image_view.hpp"
#include "core/palette.hpp"
#include "core/xor_animation.hpp"

namespace hp2 {

inline constexpr auto kScreenWidth = std::uint16_t{320};
inline constexpr auto kScreenHeight = std::uint16_t{200};

struct Point {
  std::int16_t x{};
  std::int16_t y{};
};

// The drawing areas of a split screen. Upper is the whole screen while there is no split.
enum class Viewport : std::uint8_t { kUpper, kLower };

// Pixels (8-bit palette indices) for the whole screen, one or two viewports and a palette per
// viewport. The original changes colors down the screen with the copper; here a screen splits
// once, at the row where its second palette starts (the dashboard), and
// color changes within a viewport are separate entries of that viewport's palette.
//
// Drawing calls write into the selected viewport, in its own coordinates: row 0 is the viewport's
// first screen row.
class Screen {
 public:
  static constexpr auto kWidth = kScreenWidth;
  static constexpr auto kHeight = kScreenHeight;

  // Divides the screen at `split_row`: Upper takes the rows above it, Lower the rest. Rows outside
  // 1..kHeight-1 disable the split. Selects the upper viewport.
  void EnableSplit(std::uint16_t split_row);
  // Back to one viewport over the whole screen. Palettes are kept.
  void DisableSplit();
  // Selects the viewport drawing writes into; ignored without a split.
  void SetViewport(Viewport viewport);

  [[nodiscard]] Viewport SelectedViewport() const { return selected_; }
  [[nodiscard]] std::uint16_t SplitRow() const { return split_row_; }
  // The first screen row and the row count of `viewport` (Lower has none without a split).
  [[nodiscard]] std::uint16_t FirstRow(Viewport viewport) const;
  [[nodiscard]] std::uint16_t Rows(Viewport viewport) const;

  // A row of the selected viewport, in its coordinates.
  [[nodiscard]] std::span<std::uint8_t, kWidth> Row(std::uint16_t row);
  // A screen row, whatever the viewports.
  [[nodiscard]] std::span<const std::uint8_t, kWidth> ScreenRow(std::uint16_t row) const;

  // Fills the selected viewport with color index `index`.
  void Clear(std::uint8_t index);
  // Opaque copy of `image` into the selected viewport with its top-left at `origin`, clipped to the
  // viewport, every index plus `index_offset` (modulo 256).
  void Blit(const ImageView& image, Point origin, std::uint8_t index_offset = 0);
  // Like Blit, leaving the pixels where `image` has index 0 (a masked bob, BlitBob 1:001c).
  void BlitMasked(const ImageView& image, Point origin, std::uint8_t index_offset = 0);
  // Draws `text` in `font` from `origin` on, in the selected viewport, glyphs opaque and every index
  // plus `index_offset` (DrawString, 0:0c9c); a character the font lacks is skipped but takes its place.
  void DrawText(const BitmapFont& font, std::string_view text, Point origin, std::uint8_t index_offset = 0);
  // Copies the `width` x `height` pixels at `source` to `destination`, both in the selected
  // viewport; the parts outside it are skipped. The areas may overlap.
  void Copy(Point source, Point destination, std::uint16_t width, std::uint16_t height);
  // Sets the pixels of `row` from `left` to `right`, both included, to `index`, clipped to the
  // selected viewport.
  void DrawHorizontalLine(std::int16_t left, std::int16_t right, std::int16_t row, std::uint8_t index);
  // Sets the pixels of `column` from `top` to `bottom`, both included, to `index`, clipped to the
  // selected viewport.
  void DrawVerticalLine(std::int16_t top, std::int16_t bottom, std::int16_t column, std::uint8_t index);
  // XORs the pixels from screen pixel `offset` (row * kWidth + x) on with `masks`, whatever the
  // viewports; the part past the last pixel is skipped.
  void XorScreen(std::uint32_t offset, std::span<const std::uint8_t> masks);
  // XORs frame `frame` of `animation` into the screen.
  void ApplyFrame(const XorAnimation& animation, const XorFrame& frame);

  [[nodiscard]] ScreenPalette& Palette(Viewport viewport) { return palettes_[Index(viewport)]; }
  [[nodiscard]] const ScreenPalette& Palette(Viewport viewport) const { return palettes_[Index(viewport)]; }

 private:
  [[nodiscard]] static constexpr std::size_t Index(Viewport viewport) { return static_cast<std::size_t>(viewport); }
  // Blit with `combine(index, pixel)` giving each covered pixel.
  template <typename Combine>
  void BlitWith(const ImageView& image, Point origin, Combine combine);

  std::array<std::uint8_t, std::size_t{kWidth} * kHeight> pixels_{};
  std::array<ScreenPalette, 2> palettes_;
  std::uint16_t split_row_{};  // 0: no split
  Viewport selected_{Viewport::kUpper};
};

}  // namespace hp2
