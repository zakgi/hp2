#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace hp2 {

// One colour with 8-bit components.
struct Rgb {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};

  friend constexpr bool operator==(const Rgb&, const Rgb&) = default;
};

static_assert(std::is_standard_layout_v<Rgb>);
static_assert(sizeof(Rgb) == 3);

// The fades run over this many levels: 0 is full brightness, kFadeLevels - 1 black. The original's
// FadePaletteList (0:0d06) steps through 8.
inline constexpr auto kFadeLevels = std::uint8_t{8};

// `color` at fade `level`, each channel scaled evenly toward black.
[[nodiscard]] constexpr Rgb FadedColor(Rgb color, std::uint8_t level) {
  const auto remaining = (kFadeLevels - 1) - std::min<int>(level, kFadeLevels - 1);
  const auto fade = [remaining](std::uint8_t channel) {
    return static_cast<std::uint8_t>(channel * remaining / (kFadeLevels - 1));
  };
  return Rgb{.red = fade(color.red), .green = fade(color.green), .blue = fade(color.blue)};
}

// The original's screen has 4 bitplanes, so its pictures use 16 colour registers (InitDisplay
// 1:0e90).
inline constexpr auto kColorRegisterCount = std::size_t{16};

// One segment of an original palette list, its colours decoded: from screen row `first_row` on,
// registers first_register .. first_register + count - 1 take `colors`.
struct PaletteSegment {
  std::uint16_t first_row{};
  std::uint8_t first_register{};
  std::uint8_t count{};
  std::array<Rgb, kColorRegisterCount> colors{};

  // Colour `index` of the segment at fade `level`.
  [[nodiscard]] constexpr Rgb Color(std::size_t index, std::uint8_t level = 0) const {
    return FadedColor(colors[index], level);
  }
  friend constexpr bool operator==(const PaletteSegment&, const PaletteSegment&) = default;
};

// The colours one viewport's pixels are shown in: one for every value a pixel can take, black
// until set.
class ScreenPalette {
 public:
  static constexpr auto kColorCount = std::size_t{256};
  static_assert(kColorCount > std::numeric_limits<std::uint8_t>::max(), "every pixel value has a colour");

  // Takes every entry back to black.
  void Reset() { colors_.fill(Rgb{}); }

  // Copies `colors` to the entries from `offset` on, as many as there are entries for.
  void Overlay(std::span<const Rgb> colors, std::size_t offset) {
    if (offset < colors_.size()) {
      const auto count = std::min(colors.size(), colors_.size() - offset);
      std::ranges::copy(colors.first(count), colors_.begin() + static_cast<std::ptrdiff_t>(offset));
    }
  }

  // Sets the entries of `segment`'s registers, plus `index_offset`, to its colours at fade `level`.
  void Overlay(const PaletteSegment& segment, std::uint8_t level, std::size_t index_offset = 0) {
    auto colors = std::array<Rgb, kColorRegisterCount>{};
    const auto count = std::min<std::size_t>(segment.count, colors.size());
    for (auto index = std::size_t{0}; index < count; ++index) {
      colors[index] = segment.Color(index, level);
    }
    Overlay(std::span{colors}.first(count), index_offset + segment.first_register);
  }

  void SetColor(std::uint8_t index, Rgb color) { colors_[index] = color; }

  [[nodiscard]] Rgb Color(std::uint8_t index) const { return colors_[index]; }
  [[nodiscard]] std::span<const Rgb, kColorCount> Colors() const { return colors_; }

 private:
  std::array<Rgb, kColorCount> colors_{};
};

}  // namespace hp2
