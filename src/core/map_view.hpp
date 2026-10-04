#pragma once

#include <cstdint>

#include "core/road.hpp"
#include "core/screen.hpp"
#include "core/world.hpp"

namespace hp2 {

// The map mode's own palette entries.
inline constexpr auto kMapBlack = std::uint8_t{0};
inline constexpr auto kMapDesert = std::uint8_t{1};
inline constexpr auto kMapRoad = std::uint8_t{2};
inline constexpr auto kMapPlayer = std::uint8_t{3};
inline constexpr auto kMapTarget = std::uint8_t{4};

// The map's scale and place: the 40 x 40 cells the game uses, 5 pixels each, in a square in the
// middle of the screen.
inline constexpr auto kMapCellPixels = std::int16_t{5};
inline constexpr auto kMapCells = std::int16_t{40};
inline constexpr auto kMapLeft = static_cast<std::int16_t>((Screen::kWidth - (kMapCells * kMapCellPixels)) / 2);

// The road map from above, north up, for the map mode (M), with the player's car and the
// criminal's. At this scale the road is narrower than a pixel, so each road cell is drawn as lines
// from its center to its open sides.
class MapView {
 public:
  MapView(Screen& screen, const Road& road) : screen_(screen), road_(road) {}

  // Installs the map's palette over the whole screen and draws the map with the cars.
  void Draw(WorldPoint player, WorldPoint target);

 private:
  // The center pixel of `cell`, and a line from it to each open side.
  void DrawCell(Cell cell, SideMask exits);
  void DrawMarker(WorldPoint position, std::uint8_t color);

  Screen& screen_;
  const Road& road_;
};

// The screen pixel of `point` on the map.
[[nodiscard]] Point GetMapPixel(WorldPoint point);

}  // namespace hp2
