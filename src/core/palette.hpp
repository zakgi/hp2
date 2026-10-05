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

  constexpr bool operator==(const Rgb&) const = default;
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

  void SetColor(std::uint8_t index, Rgb color) { colors_[index] = color; }

  [[nodiscard]] Rgb Color(std::uint8_t index) const { return colors_[index]; }
  [[nodiscard]] std::span<const Rgb, kColorCount> Colors() const { return colors_; }

 private:
  std::array<Rgb, kColorCount> colors_{};
};

}  // namespace hp2
