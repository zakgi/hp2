#include "core/map_view.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/palette.hpp"

namespace hp2 {

namespace {

// In palette entry order: black around the map, the desert and the road in the driving view's
// colors near the car (viewPalette, 0:a520), the player white, the criminal red.
constexpr auto kColors = std::to_array<std::uint16_t>({0x000, 0xa63, 0x555, 0xfff, 0xf00});
constexpr auto kMarkerRadius = std::int16_t{1};

// The center pixel of `cell`, and a line from it to each open side.
void DrawCell(Screen& screen, Cell cell, SideMask exits) {
  const auto opens = [exits](Side side) {
    return (exits & std::to_underlying(side)) != 0;
  };
  const auto middle = kMapCellPixels / 2;
  const auto center_x = kMapLeft + (cell.x * kMapCellPixels) + middle;
  const auto center_y = ((kMapCells - 1 - cell.y) * kMapCellPixels) + middle;
  screen.DrawHorizontalLine(static_cast<std::int16_t>(center_x - (opens(Side::kWest) ? middle : 0)),
                            static_cast<std::int16_t>(center_x + (opens(Side::kEast) ? middle : 0)),
                            static_cast<std::int16_t>(center_y), kMapRoad);
  screen.DrawVerticalLine(static_cast<std::int16_t>(center_y - (opens(Side::kNorth) ? middle : 0)),
                          static_cast<std::int16_t>(center_y + (opens(Side::kSouth) ? middle : 0)),
                          static_cast<std::int16_t>(center_x), kMapRoad);
}

void DrawMarker(Screen& screen, WorldPoint position, std::uint8_t color) {
  const auto pixel = GetMapPixel(position);
  for (auto row = pixel.y - kMarkerRadius; row <= pixel.y + kMarkerRadius; ++row) {
    screen.DrawHorizontalLine(static_cast<std::int16_t>(pixel.x - kMarkerRadius),
                              static_cast<std::int16_t>(pixel.x + kMarkerRadius), static_cast<std::int16_t>(row),
                              color);
  }
}

}  // namespace

void DrawMap(Screen& screen, const Road& road, WorldPoint player, WorldPoint target) {
  screen.DisableSplit();
  auto& palette = screen.Palette(Viewport::kUpper);
  palette.Reset();
  for (auto index = std::size_t{0}; index < kColors.size(); ++index) {
    palette.SetColor(static_cast<std::uint8_t>(index), FromAmiga(kColors[index]));
  }
  screen.Clear(kMapBlack);
  const auto right = static_cast<std::int16_t>(kMapLeft + (kMapCells * kMapCellPixels) - 1);
  for (auto row = std::int16_t{0}; row < kMapCells * kMapCellPixels; ++row) {
    screen.DrawHorizontalLine(kMapLeft, right, row, kMapDesert);
  }
  for (auto row = std::int16_t{0}; row < kMapCells; ++row) {
    for (auto column = std::int16_t{0}; column < kMapCells; ++column) {
      const auto cell = Cell{.x = column, .y = row};
      const auto exits = road.GetExits(cell);
      if (exits != 0) {
        DrawCell(screen, cell, exits);
      }
    }
  }
  DrawMarker(screen, target, kMapTarget);
  DrawMarker(screen, player, kMapPlayer);
}

Point GetMapPixel(WorldPoint point) {
  const auto column = std::floor(point.x * kMapCellPixels / kCellUnits);
  const auto row = std::floor(point.y * kMapCellPixels / kCellUnits);
  return Point{.x = static_cast<std::int16_t>(kMapLeft + column),
               .y = static_cast<std::int16_t>((kMapCells * kMapCellPixels) - 1 - row)};
}

}  // namespace hp2
