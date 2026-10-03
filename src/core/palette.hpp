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

// An Amiga colour register value, 0x0RGB with 4 bits per gun, to 8-bit components (each gun * 17,
// so 0xf becomes 0xff).
[[nodiscard]] constexpr Rgb FromAmiga(std::uint16_t color) {
  constexpr auto kGunMask = std::uint16_t{0xf};
  constexpr auto kGunScale = std::uint8_t{17};
  return Rgb{.red = static_cast<std::uint8_t>(((color >> 8) & kGunMask) * kGunScale),
             .green = static_cast<std::uint8_t>(((color >> 4) & kGunMask) * kGunScale),
             .blue = static_cast<std::uint8_t>((color & kGunMask) * kGunScale)};
}

// How an original palette stores its colours: Amiga colour registers (4 bits per gun), or Atari ST
// colours (3 bits per gun) that the game converts with PaletteSTToAmiga (0:1006) when it
// installs them.
enum class ColorFormat : std::uint8_t { kAmiga, kAtariSt };

// The darkest fade level the original uses: FadePaletteList (0:0d06) runs levels 7..0 to fade in
// and 0..7 to fade out.
inline constexpr auto kFadeLevels = std::uint8_t{8};

// A stored colour as the original shows it at fade `level`: FadePaletteList subtracts the level
// from every gun (floor 0) in the stored format, then ST colours go through PaletteSTToAmiga's gun
// table {0,2,4,6,8,a,c,f}. An ST colour is black at level 7; an Amiga colour is not (its guns
// reach 15).
[[nodiscard]] constexpr Rgb FadedColor(std::uint16_t color, ColorFormat format, std::uint8_t level = 0) {
  constexpr auto kStGun = std::array<std::uint16_t, 8>{0x0, 0x2, 0x4, 0x6, 0x8, 0xa, 0xc, 0xf};
  constexpr auto kGunMask = std::uint16_t{0xf};
  constexpr auto kStGunMask = std::uint16_t{0x7};
  auto amiga = std::uint16_t{0};
  for (const auto shift : {8, 4, 0}) {
    const auto gun = (color >> shift) & kGunMask;
    const auto faded = static_cast<std::uint16_t>(gun > level ? gun - level : 0);
    const auto converted = format == ColorFormat::kAtariSt ? kStGun[faded & kStGunMask] : faded;
    amiga = static_cast<std::uint16_t>(amiga | (converted << shift));
  }
  return FromAmiga(amiga);
}

// The original's screen has 4 bitplanes, so its pictures use 16 colour registers (InitDisplay
// 1:0e90).
inline constexpr auto kColorRegisterCount = std::size_t{16};

// One segment of an original copper palette list (docs/system.md, "Display"), as stored: from
// screen row `first_row` on, colour registers first_register .. first_register + count - 1 take
// `colors`. How a screen maps segments onto palette entries and viewports is up to the component
// showing it.
struct PaletteSegment {
  std::uint16_t first_row{};
  std::uint8_t first_register{};
  std::uint8_t count{};
  ColorFormat format{ColorFormat::kAmiga};
  std::array<std::uint16_t, kColorRegisterCount> colors{};

  // Colour `index` of the segment at fade `level`.
  [[nodiscard]] constexpr Rgb Color(std::size_t index, std::uint8_t level = 0) const {
    return FadedColor(colors[index], format, level);
  }
  friend constexpr bool operator==(const PaletteSegment&, const PaletteSegment&) = default;
};

// A palette list in row order, the first segment at row 0 (InstallPalette, 1:19b4).
using PaletteProgram = std::span<const PaletteSegment>;

// The colours one viewport's pixels are shown in: one for every value a pixel can take, black
// until set. `dirty` is set by every change and cleared by whoever displays it.
class ScreenPalette {
 public:
  static constexpr auto kColorCount = std::size_t{256};
  static_assert(kColorCount > std::numeric_limits<std::uint8_t>::max(), "every pixel value has a colour");

  // Takes every entry back to black.
  void Reset() {
    colors_.fill(Rgb{});
    dirty_ = true;
  }

  // Copies `colors` to the entries from `offset` on, as many as there are entries for.
  void Overlay(std::span<const Rgb> colors, std::size_t offset) {
    if (offset < colors_.size()) {
      const auto count = std::min(colors.size(), colors_.size() - offset);
      std::ranges::copy(colors.first(count), colors_.begin() + static_cast<std::ptrdiff_t>(offset));
      dirty_ = true;
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

  void SetColor(std::uint8_t index, Rgb color) {
    colors_[index] = color;
    dirty_ = true;
  }

  [[nodiscard]] Rgb Color(std::uint8_t index) const { return colors_[index]; }
  [[nodiscard]] std::span<const Rgb, kColorCount> Colors() const { return colors_; }
  [[nodiscard]] bool Dirty() const { return dirty_; }
  void ClearDirty() { dirty_ = false; }

 private:
  std::array<Rgb, kColorCount> colors_{};
  bool dirty_{true};
};

}  // namespace hp2
