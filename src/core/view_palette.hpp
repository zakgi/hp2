#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "core/palette.hpp"

namespace hp2 {

// Where the driving screen's colors lie (viewPalette, 0:a520; docs/renderer.md, "Palette"). The
// original changes them down the screen: the roof strip's set, the view's, 25 changes of the sky's
// color, 11 of the ground's four, then the dashboard's. Here the dashboard is the lower viewport,
// and the upper one holds every set the rows above it use, each at its own entries:
//
//   0   the view's set: the backdrop
//   16  the roof strip's set
//   32  the sky's 26 shades, from the top of the screen to the horizon
//   64  the ground's 11 sets, from the horizon to the car: a row of ground, and what stands at its
//       depth, is drawn with its set's offset
//   240 the first 8 colors of the view's set twice over, for two traffic cars with their own paint
//
// A car's picture uses colors 1 to 7, which the sky and the ground never change: white, black and
// gray, then four shades of its paint, which the original writes into the view's set for the one
// car it shows (SetCarColors, 0:e5d0). Here the criminal's car has the view's set, at entry 0, and
// each traffic car in view a copy of its own.

// The sets of the list, in its order.
inline constexpr auto kViewSetCount = std::size_t{39};
inline constexpr auto kRoofSet = std::size_t{0};
inline constexpr auto kViewSet = std::size_t{1};
inline constexpr auto kFirstGroundSet = std::size_t{27};
inline constexpr auto kDashboardSet = std::size_t{38};

// The upper viewport's entries.
inline constexpr auto kRoofOffset = std::uint8_t{16};
inline constexpr auto kSkyFirstEntry = std::uint8_t{32};
// The view's own sky, then one shade for each set that changes it.
inline constexpr auto kSkyShadeCount = kFirstGroundSet - kViewSet;
inline constexpr auto kGroundFirstOffset = std::uint8_t{64};
inline constexpr auto kGroundSetCount = kDashboardSet - kFirstGroundSet;

// The cars' colors: the registers of the paint, and the entries of the traffic cars' copies.
inline constexpr auto kCarColorCount = std::size_t{8};
inline constexpr auto kCarPaintFirst = std::uint8_t{4};
inline constexpr auto kCarPaintCount = std::size_t{4};
inline constexpr auto kTrafficFirstOffset = std::uint8_t{240};
inline constexpr auto kTrafficPaintCount = std::size_t{2};

// The paints (0:a6f4): the criminal's red, then the traffic's seven schemes, each from its lightest
// shade to its darkest.
inline constexpr auto kCarPaints = std::to_array<std::array<Rgb, kCarPaintCount>>({
    {{{.red = 255, .green = 102, .blue = 102}, {.red = 255}, {.red = 170}, {.red = 102}}},
    {{{.red = 170, .green = 204, .blue = 255},
      {.red = 136, .green = 170, .blue = 204},
      {.red = 68, .green = 102, .blue = 136},
      {.green = 34, .blue = 34}}},
    {{{.red = 170, .green = 204, .blue = 204},
      {.red = 136, .green = 170, .blue = 170},
      {.red = 68, .green = 102, .blue = 102},
      {.green = 34, .blue = 34}}},
    {{{.red = 170, .green = 255, .blue = 170},
      {.red = 136, .green = 204, .blue = 136},
      {.red = 68, .green = 136, .blue = 68},
      {.green = 68}}},
    {{{.red = 170, .green = 170, .blue = 204},
      {.red = 136, .green = 136, .blue = 170},
      {.red = 68, .green = 68, .blue = 102},
      {.blue = 34}}},
    {{{.red = 255, .green = 204, .blue = 170},
      {.red = 204, .green = 170, .blue = 136},
      {.red = 136, .green = 102, .blue = 68},
      {.red = 68, .green = 34}}},
    {{{.red = 204, .green = 255, .blue = 255},
      {.red = 170, .green = 204, .blue = 204},
      {.red = 102, .green = 136, .blue = 136},
      {.red = 34, .green = 68, .blue = 68}}},
    {{{.red = 68, .green = 170, .blue = 136},
      {.red = 34, .green = 136, .blue = 102},
      {.green = 68, .blue = 34},
      {.green = 34}}},
});

// The color registers the view's pictures and the ground use.
inline constexpr auto kLineColor = std::uint8_t{1};
inline constexpr auto kSkyColor = std::uint8_t{8};
inline constexpr auto kSandColor = std::uint8_t{14};
inline constexpr auto kAsphaltColor = std::uint8_t{15};

// The screen row where the dashboard, the lower viewport, starts.
inline constexpr auto kDashboardRow = std::uint16_t{132};

// The sky's entry on screen row `row`: the view's own sky down to row 19, then a shade lighter
// every 2 rows.
[[nodiscard]] constexpr std::uint8_t GetSkyEntry(std::int16_t row) {
  constexpr auto kFirstChangeRow = std::int16_t{20};
  constexpr auto kRowsPerShade = std::int16_t{2};
  const auto shade =
      row < kFirstChangeRow ? std::size_t{0} : static_cast<std::size_t>(1 + ((row - kFirstChangeRow) / kRowsPerShade));
  return static_cast<std::uint8_t>(kSkyFirstEntry + std::min(shade, kSkyShadeCount - 1));
}

// The offset of the ground's set on screen row `row`: the haze thins from the horizon, row 70, down
// to row 92.
[[nodiscard]] constexpr std::uint8_t GetGroundOffset(std::int16_t row) {
  constexpr auto kFirstRows = std::to_array<std::int16_t>({70, 71, 73, 75, 77, 79, 81, 83, 85, 88, 92});
  static_assert(kFirstRows.size() == kGroundSetCount);
  auto set = std::size_t{0};
  while (set + 1 < kFirstRows.size() and row >= kFirstRows[set + 1]) {
    ++set;
  }
  return static_cast<std::uint8_t>(kGroundFirstOffset + (set * kColorRegisterCount));
}

}  // namespace hp2
