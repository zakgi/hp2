#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "core/road_map.hpp"
#include "core/world.hpp"

namespace hp2 {

// A path for a computer driver through one cell, on the right-hand half of the road: pieces that
// are straight or circular arcs, joined without a kink, in world units. A curve cell's lane is one
// arc about the cell's corner; a turn at a junction is a line, a tight arc and a line, so it stays
// on the paved cross. Drivers chase a point ahead on the lane, which smooths the joins. The
// original's cars follow 24 fixed polylines (0:9b40, picked through 0:5f18).
class Lane {
 public:
  struct Piece {
    float length{};
    float curvature{};  // 1 / radius, positive turning left; 0 straight
  };
  // Where a point lies along the lane: the distance from its start, and the offset to the right of
  // it (negative: to the left).
  struct Position {
    float distance{};
    float offset{};
  };
  // Straight, turn, straight.
  static constexpr std::size_t kMaxPieces = 3;

  Lane() = default;
  // The first kMaxPieces of `pieces`, starting at `start` along `heading`.
  Lane(WorldPoint start, float heading, std::span<const Piece> pieces);

  [[nodiscard]] float GetLength() const;
  [[nodiscard]] Position Locate(WorldPoint point) const;
  // The point and the heading `distance` along the lane, clamped to its ends.
  [[nodiscard]] WorldPoint GetPoint(float distance) const;
  [[nodiscard]] float GetHeading(float distance) const;

 private:
  WorldPoint start_;
  float heading_{};
  std::array<Piece, kMaxPieces> pieces_{};
  std::uint8_t count_{};
};

// At most this many station cells: the map has 20, the same the original lists at 1:23e0.
inline constexpr std::size_t kStationCount = 20;

// Everything about the ground, for the mission and the views: the road map (CARTE.BIN), the
// road shapes, the lanes, the stations and the scenery (COOR_OBJ.BIN). A value over views of the
// assets, cheap to copy.
class Road {
 public:
  Road(RoadMapView map, RoadShapes shapes, Scenery scenery);

  // The road cell type of `cell`; 0 (no road) off the map.
  [[nodiscard]] std::uint8_t GetCellType(Cell cell) const;
  [[nodiscard]] SideMask GetExits(Cell cell) const { return kCellExits[GetCellType(cell)]; }

  // Whether `point` lies inside its cell's road outline. Replaces the original's test of two drawn
  // pixels under the hood (CheckOffRoad, 0:3fa8), so any point of any car can be tested.
  [[nodiscard]] bool IsOnRoad(WorldPoint point) const;
  // The road's outline in `cell`, in cell units: one closed polygon, the first point repeated last,
  // which IsOnRoad fills even-odd; empty where there is no road.
  [[nodiscard]] std::span<const ShapePoint> GetOutline(Cell cell) const;

  // The lane through `cell` from the side it is entered by to the side it is left by; none when the
  // cell's road does not join them.
  [[nodiscard]] std::optional<Lane> GetLane(Cell cell, Side entry, Side exit) const;
  // In a station cell, the way from `entry` through the driveway and back onto the road.
  [[nodiscard]] std::optional<Lane> GetDriveway(Cell cell, Side entry) const;

  // The station cells, row by row.
  [[nodiscard]] std::span<const Cell> GetStations() const {
    return std::span<const Cell>{stations_}.first(station_count_);
  }
  // The index into GetStations() of the station at `cell`, if there is one.
  [[nodiscard]] std::optional<std::size_t> GetStationIndex(Cell cell) const;
  // Whether `point` is at a station's pumps, where a car stops to be served or to rob
  // (CheckStationZone, 0:340a).
  [[nodiscard]] bool IsInStationArea(WorldPoint point) const;

  // The scenery of `cell`, in cell units.
  [[nodiscard]] std::span<const PlacedObject> GetScenery(Cell cell) const;

 private:
  RoadMapView map_;
  RoadShapes shapes_;
  Scenery scenery_;
  std::array<Cell, kStationCount> stations_{};
  std::size_t station_count_{};
};

}  // namespace hp2
