#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace hp2 {

// One color with 8-bit components.
struct Rgb {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};

  friend constexpr bool operator==(const Rgb&, const Rgb&) = default;
};

static_assert(std::is_standard_layout_v<Rgb>);
static_assert(sizeof(Rgb) == 3);

// `color` with each channel scaled by `brightness`, 0 (black) to 1 (unchanged).
[[nodiscard]] inline Rgb ScaleColor(Rgb color, float brightness) {
  return Rgb{.red = static_cast<std::uint8_t>(std::lround(static_cast<float>(color.red) * brightness)),
             .green = static_cast<std::uint8_t>(std::lround(static_cast<float>(color.green) * brightness)),
             .blue = static_cast<std::uint8_t>(std::lround(static_cast<float>(color.blue) * brightness))};
}

// The original's screen has 4 bitplanes, so its pictures use 16 color registers (InitDisplay
// 1:0e90).
inline constexpr auto kColorRegisterCount = std::size_t{16};

// One segment of an original palette list, its colors decoded: from screen row `first_row` on,
// registers first_register .. first_register + count - 1 take `colors`.
struct PaletteSegment {
  std::uint16_t first_row{};
  std::uint8_t first_register{};
  std::uint8_t count{};
  std::array<Rgb, kColorRegisterCount> colors{};

  // Color `index` of the segment.
  [[nodiscard]] constexpr Rgb Color(std::size_t index) const { return colors[index]; }
  friend constexpr bool operator==(const PaletteSegment&, const PaletteSegment&) = default;
};

// The colors one viewport's pixels are shown in: one for every value a pixel can take, black
// until set.
class ScreenPalette {
 public:
  static constexpr auto kColorCount = std::size_t{256};
  static_assert(kColorCount > std::numeric_limits<std::uint8_t>::max(), "every pixel value has a color");

  // Takes every entry back to black.
  void Reset() { colors_.fill(Rgb{}); }

  // Copies `colors` to the entries from `offset` on, as many as there are entries for.
  void Overlay(std::span<const Rgb> colors, std::size_t offset) {
    if (offset < colors_.size()) {
      const auto count = std::min(colors.size(), colors_.size() - offset);
      std::ranges::copy(colors.first(count), colors_.begin() + static_cast<std::ptrdiff_t>(offset));
    }
  }

  // Sets the entries of `segment`'s registers, plus `index_offset`, to its colors.
  void Overlay(const PaletteSegment& segment, std::size_t index_offset = 0) {
    const auto count = std::min<std::size_t>(segment.count, segment.colors.size());
    Overlay(std::span{segment.colors}.first(count), index_offset + segment.first_register);
  }

  void SetColor(std::uint8_t index, Rgb color) { colors_[index] = color; }

  [[nodiscard]] Rgb Color(std::uint8_t index) const { return colors_[index]; }
  [[nodiscard]] std::span<const Rgb, kColorCount> Colors() const { return colors_; }

 private:
  std::array<Rgb, kColorCount> colors_{};
};

}  // namespace hp2
