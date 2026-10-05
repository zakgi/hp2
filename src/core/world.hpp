#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

#include "core/road_map.hpp"

namespace hp2 {

// The original's world units: a map cell is 0x4000 units square, the road 1024 wide
// (docs/renderer.md, "World"). The port keeps them, so the road shapes, the scenery and the
// documents compare directly.
inline constexpr float kCellUnits = 16384.0F;
// An assumed scale, only for stating tuning values in familiar terms: the road is about 8 m wide.
inline constexpr float kUnitsPerMeter = 128.0F;

// A place on the map, or a vector between two, in world units: +x east, +y north. The original
// holds a cell and a 16-bit position inside it; a float over the whole map still resolves 1/16
// unit at its far edge.
struct WorldPoint {
  float x{};
  float y{};
};

[[nodiscard]] constexpr WorldPoint operator+(WorldPoint left, WorldPoint right) {
  return WorldPoint{.x = left.x + right.x, .y = left.y + right.y};
}
[[nodiscard]] constexpr WorldPoint operator-(WorldPoint left, WorldPoint right) {
  return WorldPoint{.x = left.x - right.x, .y = left.y - right.y};
}
[[nodiscard]] constexpr WorldPoint operator*(WorldPoint vector, float factor) {
  return WorldPoint{.x = vector.x * factor, .y = vector.y * factor};
}
[[nodiscard]] constexpr float Dot(WorldPoint left, WorldPoint right) {
  return (left.x * right.x) + (left.y * right.y);
}
// Positive when `right` turns counter-clockwise from `left`.
[[nodiscard]] constexpr float Cross(WorldPoint left, WorldPoint right) {
  return (left.x * right.y) - (left.y * right.x);
}
[[nodiscard]] inline float GetLength(WorldPoint vector) {
  return std::hypot(vector.x, vector.y);
}

// A cell of the road map: column x and row y of CARTE.BIN. Rows grow northward going by the HUD
// compass, not yet confirmed in play (docs/vehicles.md, section 14).
struct Cell {
  std::int16_t x{};
  std::int16_t y{};

  constexpr bool operator==(const Cell&) const = default;
};

[[nodiscard]] inline Cell GetCell(WorldPoint point) {
  return Cell{.x = static_cast<std::int16_t>(std::floor(point.x / kCellUnits)),
              .y = static_cast<std::int16_t>(std::floor(point.y / kCellUnits))};
}
// The cell's south-west corner, where its cell units start.
[[nodiscard]] constexpr WorldPoint GetCellOrigin(Cell cell) {
  return WorldPoint{.x = static_cast<float>(cell.x) * kCellUnits, .y = static_cast<float>(cell.y) * kCellUnits};
}

// Angles are radians counter-clockwise from east: heading pi/2 drives north. The original counts
// 720 half degrees a turn in the same sense (docs/vehicles.md, section 1).
inline constexpr float kFullTurn = 2.0F * std::numbers::pi_v<float>;

[[nodiscard]] constexpr float FromHalfDegrees(std::int32_t half_degrees) {
  return static_cast<float>(half_degrees) * (kFullTurn / 720.0F);
}
// `angle` brought into [-pi, pi).
[[nodiscard]] inline float WrapAngle(float angle) {
  return angle - (kFullTurn * std::floor((angle + std::numbers::pi_v<float>) / kFullTurn));
}
// The unit vector along `heading`.
[[nodiscard]] inline WorldPoint GetDirection(float heading) {
  return WorldPoint{.x = std::cos(heading), .y = std::sin(heading)};
}
// The angle of `vector`, the inverse of GetDirection; 0 for the zero vector.
[[nodiscard]] inline float GetAngle(WorldPoint vector) {
  return std::atan2(vector.y, vector.x);
}

// The four sides of a cell as bits, so the sides a cell type opens onto form a mask (0:5eea).
enum class Side : std::uint8_t { kEast = 1, kWest = 2, kNorth = 4, kSouth = 8 };
using SideMask = std::uint8_t;
inline constexpr auto kSides = std::to_array<Side>({Side::kEast, Side::kWest, Side::kNorth, Side::kSouth});

[[nodiscard]] constexpr bool HasSide(SideMask mask, Side side) {
  return (mask & std::to_underlying(side)) != 0;
}

// The sides each road cell type opens onto (0:5eea). The original gives type 0 (no road) all four;
// here it has none.
inline constexpr auto kCellExits =
    std::to_array<SideMask>({0x0, 0xc, 0x3, 0xa, 0x9, 0x5, 0x6, 0x7, 0xb, 0xd, 0xf, 0xc, 0x3});
static_assert(kCellExits.size() == kRoadCellTypeCount);

// The unit vector from a cell's middle toward `side`.
[[nodiscard]] constexpr WorldPoint GetOutward(Side side) {
  auto result = WorldPoint{};
  switch (side) {
    case Side::kEast:
      result.x = 1.0F;
      break;
    case Side::kWest:
      result.x = -1.0F;
      break;
    case Side::kNorth:
      result.y = 1.0F;
      break;
    case Side::kSouth:
      result.y = -1.0F;
      break;
  }
  return result;
}

[[nodiscard]] constexpr Side GetOpposite(Side side) {
  auto result = Side::kWest;
  switch (side) {
    case Side::kEast:
      result = Side::kWest;
      break;
    case Side::kWest:
      result = Side::kEast;
      break;
    case Side::kNorth:
      result = Side::kSouth;
      break;
    case Side::kSouth:
      result = Side::kNorth;
      break;
  }
  return result;
}

[[nodiscard]] constexpr Cell GetNeighbor(Cell cell, Side side) {
  auto result = cell;
  switch (side) {
    case Side::kEast:
      ++result.x;
      break;
    case Side::kWest:
      --result.x;
      break;
    case Side::kNorth:
      ++result.y;
      break;
    case Side::kSouth:
      --result.y;
      break;
  }
  return result;
}

}  // namespace hp2
