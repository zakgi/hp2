#include "core/road.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace hp2 {

namespace {

constexpr auto kNorthSouthStation = std::uint8_t{11};
constexpr auto kEastWestStation = std::uint8_t{12};

// A rectangle in cell units: left and bottom edges included, right and top excluded.
struct CellArea {
  float left;
  float bottom;
  float right;
  float top;

  [[nodiscard]] constexpr bool Contains(WorldPoint point) const {
    return point.x >= left and point.x < right and point.y >= bottom and point.y < top;
  }
};

// Where a car stands at the pumps of a station cell (CheckStationZone, 0:340a).
constexpr auto kNorthSouthPumps = CellArea{.left = 0x2400, .bottom = 0x900, .right = 0x4000, .top = 0x33e4};
constexpr auto kEastWestPumps = CellArea{.left = 0x900, .bottom = 0, .right = 0x33e4, .top = 0x1c00};

WorldPoint ToWorldPoint(ShapePoint point) {
  return WorldPoint{.x = static_cast<float>(point.x), .y = static_cast<float>(point.y)};
}

// Whether `point`, in cell units, is inside `outline` by the even-odd rule: a ray toward +x crosses
// the outline an odd number of times.
bool IsInside(std::span<const ShapePoint> outline, WorldPoint point) {
  auto inside = false;
  for (auto index = std::size_t{1}; index < outline.size(); ++index) {
    const auto start = ToWorldPoint(outline[index - 1]);
    const auto end = ToWorldPoint(outline[index]);
    if ((start.y > point.y) != (end.y > point.y)) {
      const auto crossing = start.x + ((point.y - start.y) * (end.x - start.x) / (end.y - start.y));
      inside = inside != (point.x < crossing);
    }
  }
  return inside;
}

}  // namespace

Road::Road(RoadMapView map, RoadShapes shapes, Scenery scenery) : map_(map), shapes_(shapes), scenery_(scenery) {
  // Row by row, the order of the original's list (1:23e0).
  const auto size = static_cast<std::int16_t>(RoadMapView::kSize);
  for (auto row = std::int16_t{0}; row < size; ++row) {
    for (auto column = std::int16_t{0}; column < size; ++column) {
      const auto cell = Cell{.x = column, .y = row};
      const auto type = GetCellType(cell);
      if ((type == kNorthSouthStation or type == kEastWestStation) and station_count_ < kStationCount) {
        stations_[station_count_] = cell;
        ++station_count_;
      }
    }
  }
}

std::uint8_t Road::GetCellType(Cell cell) const {
  auto type = std::uint8_t{0};
  const auto size = static_cast<std::int16_t>(RoadMapView::kSize);
  if (cell.x >= 0 and cell.x < size and cell.y >= 0 and cell.y < size) {
    type = map_.GetCell(static_cast<std::size_t>(cell.x), static_cast<std::size_t>(cell.y));
  }
  return type;
}

bool Road::IsOnRoad(WorldPoint point) const {
  const auto cell = GetCell(point);
  return IsInside(shapes_.GetOutline(GetCellType(cell)), point - GetCellOrigin(cell));
}

std::optional<std::size_t> Road::GetStationIndex(Cell cell) const {
  const auto stations = GetStations();
  const auto found = std::ranges::find(stations, cell);
  return found == stations.end() ? std::nullopt : std::optional{static_cast<std::size_t>(found - stations.begin())};
}

bool Road::IsInStationArea(WorldPoint point) const {
  const auto cell = GetCell(point);
  const auto type = GetCellType(cell);
  const auto local = point - GetCellOrigin(cell);
  return (type == kNorthSouthStation and kNorthSouthPumps.Contains(local)) or
         (type == kEastWestStation and kEastWestPumps.Contains(local));
}

std::span<const PlacedObject> Road::GetScenery(Cell cell) const {
  return scenery_.GetObjects(GetCellType(cell));
}

}  // namespace hp2
