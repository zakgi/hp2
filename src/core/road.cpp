#include "core/road.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

// Lanes (docs/highway.md, "Road"). The road's center line crosses a cell through its middle; a lane
// runs kLaneOffset to its right, down the middle of the right-hand half of the road.
constexpr auto kHalfCell = kCellUnits / 2.0F;
constexpr auto kLaneOffset = 256.0F;
constexpr auto kQuarterTurn = kFullTurn / 4.0F;
// Turns at a junction, tight enough to stay on the paved cross, whose corners are cut 512 units back.
constexpr auto kRightTurnRadius = 768.0F;
constexpr auto kLeftTurnRadius = 1536.0F;

// A station's driveway, from the outlines of types 11 and 12: the middle of a ramp leaves the
// road's edge kRampStart into the cell and gains kRampAcross units sideways for every kRampAlong
// along the road, up to the pad, whose middle lies kPadOffset from the center line; a second ramp,
// its mirror image, leads back to the road.
constexpr auto kRoadHalfWidth = 512.0F;
constexpr auto kRampStart = 2688.0F;
constexpr auto kRampAlong = 6.0F;
constexpr auto kRampAcross = 7.0F;
constexpr auto kPadOffset = 5632.0F;
constexpr auto kDrivewayTurnRadius = 1024.0F;

// A place on a lane and the way the lane runs there.
struct Pose {
  WorldPoint point;
  float heading{};
};

// The unit vector to the left of `heading`.
WorldPoint GetLeft(float heading) {
  return WorldPoint{.x = -std::sin(heading), .y = std::cos(heading)};
}

// The pose `distance` along `piece` from `start`.
Pose Advance(Pose start, const Lane::Piece& piece, float distance) {
  auto result = Pose{.point = start.point + (GetDirection(start.heading) * distance), .heading = start.heading};
  if (piece.curvature != 0.0F) {
    // An arc about a center 1 / curvature to the left.
    const auto radius = 1.0F / piece.curvature;
    const auto heading = start.heading + (piece.curvature * distance);
    result = Pose{.point = start.point + ((GetLeft(start.heading) - GetLeft(heading)) * radius), .heading = heading};
  }
  return result;
}

// The pose `distance` along `pieces` from `start`, clamped to their ends.
Pose Walk(Pose start, std::span<const Lane::Piece> pieces, float distance) {
  auto pose = start;
  auto remaining = std::max(distance, 0.0F);
  for (const auto& piece : pieces) {
    const auto step = std::min(remaining, piece.length);
    pose = Advance(pose, piece, step);
    remaining -= step;
  }
  return pose;
}

// How far along `piece` from `start` its point nearest to `point` lies.
float GetNearest(Pose start, const Lane::Piece& piece, WorldPoint point) {
  auto along = Dot(point - start.point, GetDirection(start.heading));
  if (piece.curvature != 0.0F) {
    // The angle about the arc's center from the arc's middle to `point`, as a length of arc.
    const auto radius = 1.0F / piece.curvature;
    const auto center = start.point + (GetLeft(start.heading) * radius);
    const auto middle = Advance(start, piece, piece.length / 2.0F);
    const auto turn = WrapAngle(GetAngle(point - center) - GetAngle(middle.point - center));
    along = (piece.length / 2.0F) + (turn * radius);
  }
  return std::clamp(along, 0.0F, piece.length);
}

// Where a lane entered by `entry` starts in `cell`, heading inward.
Pose GetLaneStart(Cell cell, Side entry) {
  const auto outward = GetOutward(entry);
  const auto right = WorldPoint{.x = -outward.y, .y = outward.x};
  return Pose{.point = GetCellOrigin(cell) + WorldPoint{.x = kHalfCell, .y = kHalfCell} + (outward * kHalfCell) +
                       (right * kLaneOffset),
              .heading = GetAngle(outward * -1.0F)};
}

}  // namespace

Lane::Lane(WorldPoint start, float heading, std::span<const Piece> pieces)
    : start_(start), heading_(heading), count_(static_cast<std::uint8_t>(std::min(pieces.size(), kMaxPieces))) {
  std::ranges::copy(pieces.first(count_), pieces_.begin());
}

float Lane::GetLength() const {
  auto length = 0.0F;
  for (const auto& piece : GetPieces()) {
    length += piece.length;
  }
  return length;
}

Lane::Position Lane::Locate(WorldPoint point) const {
  auto result = Position{};
  auto nearest = std::numeric_limits<float>::max();
  auto start = Pose{.point = start_, .heading = heading_};
  auto covered = 0.0F;
  for (const auto& piece : GetPieces()) {
    const auto along = GetNearest(start, piece, point);
    const auto pose = Advance(start, piece, along);
    const auto apart = point - pose.point;
    if (const auto gap = hp2::GetLength(apart); gap < nearest) {
      nearest = gap;
      result = Position{.distance = covered + along, .offset = -Dot(apart, GetLeft(pose.heading))};
    }
    start = Advance(start, piece, piece.length);
    covered += piece.length;
  }
  return result;
}

WorldPoint Lane::GetPoint(float distance) const {
  return Walk(Pose{.point = start_, .heading = heading_}, GetPieces(), distance).point;
}

float Lane::GetHeading(float distance) const {
  return WrapAngle(Walk(Pose{.point = start_, .heading = heading_}, GetPieces(), distance).heading);
}

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

std::span<const ShapePoint> Road::GetOutline(Cell cell) const {
  return shapes_.GetOutline(GetCellType(cell));
}

std::optional<Lane> Road::GetLane(Cell cell, Side entry, Side exit) const {
  auto result = std::optional<Lane>{};
  const auto exits = GetExits(cell);
  if (entry != exit and HasSide(exits, entry) and HasSide(exits, exit)) {
    const auto start = GetLaneStart(cell, entry);
    auto pieces = std::array<Lane::Piece, 3>{};
    auto count = std::size_t{1};
    if (exit == GetOpposite(entry)) {
      pieces[0] = Lane::Piece{.length = kCellUnits};
    } else {
      // 1 for a turn to the left, -1 to the right; `reach` is how far into the cell the two straight
      // ways through it would cross.
      const auto side = Cross(GetDirection(start.heading), GetOutward(exit)) > 0.0F ? 1.0F : -1.0F;
      const auto reach = kHalfCell + (side * kLaneOffset);
      if (std::popcount(exits) == 2) {
        // A curve cell: one arc about the cell's corner.
        pieces[0] = Lane::Piece{.length = reach * kQuarterTurn, .curvature = side / reach};
      } else {
        const auto radius = side > 0.0F ? kLeftTurnRadius : kRightTurnRadius;
        pieces = {{{.length = reach - radius},
                   {.length = radius * kQuarterTurn, .curvature = side / radius},
                   {.length = reach - radius}}};
        count = pieces.size();
      }
    }
    result = Lane{start.point, start.heading, std::span{pieces}.first(count)};
  }
  return result;
}

std::optional<Lane> Road::GetDriveway(Cell cell, Side entry) const {
  auto result = std::optional<Lane>{};
  const auto type = GetCellType(cell);
  if ((type == kNorthSouthStation or type == kEastWestStation) and HasSide(GetExits(cell), entry)) {
    const auto start = GetLaneStart(cell, entry);
    // The station lies east of a north-south road and south of an east-west one: 1 when that is to
    // the left of the way in, -1 to the right.
    const auto station = GetOutward(type == kNorthSouthStation ? Side::kEast : Side::kSouth);
    const auto side = Cross(GetDirection(start.heading), station) > 0.0F ? 1.0F : -1.0F;
    // The lane's distance from the center line, toward the station.
    const auto lane = -side * kLaneOffset;
    // Along the road to the ramp's middle line, up the ramp to the pad's, along the pad, and the
    // same back; each turn takes `tangent` off the straights it joins.
    const auto turn = std::atan2(kRampAcross, kRampAlong);
    const auto tangent = kDrivewayTurnRadius * std::tan(turn / 2.0F);
    const auto approach = kRampStart + ((lane - kRoadHalfWidth) * kRampAlong / kRampAcross);
    const auto ramp = (kPadOffset - lane) * std::hypot(kRampAlong, kRampAcross) / kRampAcross;
    const auto pad = kCellUnits - (2.0F * (kRampStart + ((kPadOffset - kRoadHalfWidth) * kRampAlong / kRampAcross)));
    const auto arc = kDrivewayTurnRadius * turn;
    const auto toward = side / kDrivewayTurnRadius;
    const auto pieces = std::to_array<Lane::Piece>({
        {.length = approach - tangent},
        {.length = arc, .curvature = toward},
        {.length = ramp - (2.0F * tangent)},
        {.length = arc, .curvature = -toward},
        {.length = pad - (2.0F * tangent)},
        {.length = arc, .curvature = -toward},
        {.length = ramp - (2.0F * tangent)},
        {.length = arc, .curvature = toward},
        {.length = approach - tangent},
    });
    result = Lane{start.point, start.heading, pieces};
  }
  return result;
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
