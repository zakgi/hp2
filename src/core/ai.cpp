#include "core/ai.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

namespace hp2 {

namespace {

constexpr auto kMapSize = static_cast<std::int16_t>(RoadMapView::kSize);
constexpr auto kHalfCell = kCellUnits / 2.0F;

// The point a driver chases lies this far ahead on its lanes, plus kReachSeconds of travel.
constexpr auto kMinReach = 400.0F;
constexpr auto kReachSeconds = 0.25F;
// The share of the car's grip a driver takes a turn with, and of its brakes it slows down with.
constexpr auto kGripShare = 0.7F;
constexpr auto kBrakingShare = 0.7F;
// The speed error, units per second, at which the throttle is wide open or the brakes full on.
constexpr auto kThrottleBand = 200.0F;

bool IsOnMap(Cell cell) {
  return cell.x >= 0 and cell.x < kMapSize and cell.y >= 0 and cell.y < kMapSize;
}

std::size_t GetIndex(Cell cell) {
  return (static_cast<std::size_t>(cell.y) * RoadMapView::kSize) + static_cast<std::size_t>(cell.x);
}

// The open side of `cell` nearest to `position`: the side a car just in came by.
Side GetNearestSide(SideMask exits, Cell cell, WorldPoint position) {
  auto result = Side::kSouth;
  auto nearest = std::numeric_limits<float>::max();
  const auto middle = GetCellOrigin(cell) + WorldPoint{.x = kHalfCell, .y = kHalfCell};
  for (const auto side : kSides) {
    const auto distance = GetLength(position - (middle + (GetOutward(side) * kHalfCell)));
    if (HasSide(exits, side) and distance < nearest) {
      nearest = distance;
      result = side;
    }
  }
  return result;
}

// A lane through a cell and the side it leaves by.
struct PlannedLane {
  Lane lane;
  Side exit{Side::kNorth};
};

// The lane through `cell` from `entry` toward the goal of `route`; no lane where the road does not
// go on.
PlannedLane PlanLane(const RouteField& route, const Road& road, Cell cell, Side entry, bool use_driveway) {
  auto exit = GetOpposite(entry);
  auto lane = std::optional<Lane>{};
  if (const auto chosen = route.ChooseExit(road, cell, entry)) {
    exit = *chosen;
    lane = road.GetLane(cell, entry, exit);
  } else {
    if (use_driveway) {
      lane = road.GetDriveway(cell, entry);
    }
    // No driveway: on through the goal cell, straight where the road allows.
    const auto exits = road.GetExits(cell);
    for (const auto side : kSides) {
      if (not lane and side != entry and HasSide(exits, side) and
          (side == GetOpposite(entry) or not HasSide(exits, GetOpposite(entry)))) {
        exit = side;
        lane = road.GetLane(cell, entry, exit);
      }
    }
  }
  return PlannedLane{.lane = lane.value_or(Lane{}), .exit = exit};
}

// The speed a driver `distance` along its first lane aims for: its cruise speed, less where a turn
// or a stop ahead needs the brakes now.
float GetTargetSpeed(const AiDriver& driver, float distance, const VehicleTuning& tuning) {
  const auto braking = tuning.braking * kBrakingShare;
  auto target = driver.cruise_speed;
  // Each piece's start, ahead of the car.
  auto start = -distance;
  for (const auto& lane : driver.lanes) {
    for (const auto& piece : lane.GetPieces()) {
      if (piece.curvature != 0.0F and start + piece.length > 0.0F) {
        const auto turn_speed_squared = tuning.grip * kGripShare / std::abs(piece.curvature);
        target = std::min(target, std::sqrt(turn_speed_squared + (2.0F * braking * std::max(start, 0.0F))));
      }
      start += piece.length;
    }
  }
  if (driver.plan == AiDriver::Plan::kPullIn) {
    const auto stop = (driver.lanes[0].GetLength() / 2.0F) - distance;
    target = std::min(target, std::sqrt(2.0F * braking * std::max(stop, 0.0F)));
  } else if (driver.plan == AiDriver::Plan::kStopped) {
    target = 0.0F;
  }
  return target;
}

bool IsWithinOneCell(Cell first, Cell second) {
  return std::abs(first.x - second.x) < 2 and std::abs(first.y - second.y) < 2;
}

// The side of its cell a car heading along `heading` faces.
Side GetFacingSide(float heading) {
  auto result = Side::kEast;
  auto most = -2.0F;
  for (const auto side : kSides) {
    if (const auto along = Dot(GetDirection(heading), GetOutward(side)); along > most) {
      most = along;
      result = side;
    }
  }
  return result;
}

// One of the open sides of `exits` other than `first` and `second`, each as likely; none when
// there is no other.
std::optional<Side> ChooseOtherSide(SideMask exits, Side first, Side second, Random& random) {
  auto result = std::optional<Side>{};
  const auto fits = [&](Side side) {
    return HasSide(exits, side) and side != first and side != second;
  };
  if (const auto count = std::ranges::count_if(kSides, fits); count > 0) {
    auto skipped = random.Below(static_cast<std::uint32_t>(count));
    for (const auto side : kSides) {
      if (fits(side) and not result and skipped-- == 0) {
        result = side;
      }
    }
  }
  return result;
}

}  // namespace

void RouteField::Build(const Road& road, Cell goal) {
  goal_ = goal;
  distances_.fill(kUnreachable);
  if (IsOnMap(goal) and road.GetExits(goal) != 0) {
    distances_[GetIndex(goal)] = 0;
    // Sweeps over the map, forward then backward, until nothing changes: the search keeps no queue.
    auto changed = true;
    while (changed) {
      changed = false;
      for (auto row = std::int16_t{0}; row < kMapSize; ++row) {
        for (auto column = std::int16_t{0}; column < kMapSize; ++column) {
          changed = Relax(road, Cell{.x = column, .y = row}) or changed;
        }
      }
      for (auto row = static_cast<std::int16_t>(kMapSize - 1); row >= 0; --row) {
        for (auto column = static_cast<std::int16_t>(kMapSize - 1); column >= 0; --column) {
          changed = Relax(road, Cell{.x = column, .y = row}) or changed;
        }
      }
    }
  }
}

bool RouteField::Relax(const Road& road, Cell cell) {
  auto nearest = kUnreachable;
  const auto exits = road.GetExits(cell);
  for (const auto side : kSides) {
    const auto neighbor = GetNeighbor(cell, side);
    if (HasSide(exits, side) and HasSide(road.GetExits(neighbor), GetOpposite(side))) {
      nearest = std::min(nearest, GetDistance(neighbor));
    }
  }
  auto& distance = distances_[GetIndex(cell)];
  const auto changed = nearest != kUnreachable and nearest + 1 < distance;
  if (changed) {
    distance = static_cast<std::uint16_t>(nearest + 1);
  }
  return changed;
}

std::uint16_t RouteField::GetDistance(Cell cell) const {
  return IsOnMap(cell) ? distances_[GetIndex(cell)] : kUnreachable;
}

std::optional<Side> RouteField::ChooseExit(const Road& road, Cell cell, Side entry) const {
  auto result = std::optional<Side>{};
  const auto exits = road.GetExits(cell);
  if (cell != goal_) {
    auto nearest = kUnreachable;
    for (const auto side : kSides) {
      const auto distance = GetDistance(GetNeighbor(cell, side));
      if (side != entry and HasSide(exits, side) and distance < nearest) {
        nearest = distance;
        result = side;
      }
    }
    if (not result and HasSide(exits, entry)) {
      result = entry;
    }
  }
  return result;
}

void PlanLanes(AiDriver& driver, const Road& road, WorldPoint position, bool use_driveway) {
  const auto cell = GetCell(position);
  if (const auto exits = road.GetExits(cell); exits != 0) {
    driver.cell = cell;
    driver.entry = GetNearestSide(exits, cell, position);
    const auto planned = PlanLane(driver.route, road, cell, driver.entry, use_driveway);
    driver.lanes[0] = planned.lane;
    driver.exit = planned.exit;
    PlanNextLane(driver, road, use_driveway);
  }
}

void PlanNextLane(AiDriver& driver, const Road& road, bool use_driveway) {
  driver.lanes[1] =
      PlanLane(driver.route, road, GetNeighbor(driver.cell, driver.exit), GetOpposite(driver.exit), use_driveway).lane;
}

Controls FollowLane(const AiDriver& driver, const Vehicle& vehicle, const VehicleTuning& tuning) {
  auto controls = Controls{};
  auto target = 0.0F;
  const auto& lane = driver.lanes[0];
  const auto& next = driver.lanes[1];
  if (const auto length = lane.GetLength(); length > 0.0F) {
    const auto place = lane.Locate(vehicle.position);
    // The point to chase, on the next lane once past the end of this one.
    const auto ahead = place.distance + kMinReach + (std::abs(vehicle.speed) * kReachSeconds);
    const auto chase =
        ahead > length and next.GetLength() > 0.0F ? next.GetPoint(ahead - length) : lane.GetPoint(ahead);
    // The arc that leaves along the car's heading and passes through that point curves by twice
    // the point's offset to the left over the square of its distance.
    const auto toward = chase - vehicle.position;
    const auto curvature = 2.0F * Cross(GetDirection(vehicle.heading), toward) / std::max(Dot(toward, toward), 1.0F);
    controls.steer = std::clamp(curvature * tuning.turning_radius, -1.0F, 1.0F);
    target = GetTargetSpeed(driver, place.distance, tuning);
  }
  if (target > 0.0F) {
    controls.throttle = std::clamp((target - vehicle.speed) / kThrottleBand, -1.0F, 1.0F);
  } else if (vehicle.speed > 0.0F) {
    // The brakes, until the car stands: held on after that they would reverse it.
    controls.throttle = -1.0F;
  }
  return controls;
}

std::optional<std::size_t> NearestByRoad::ChooseStation(const PolicyContext& context,
                                                        [[maybe_unused]] Random& random) const {
  auto result = std::optional<std::size_t>{};
  auto nearest = RouteField::kUnreachable;
  const auto stations = context.road.GetStations();
  for (auto index = std::size_t{0}; index < stations.size(); ++index) {
    const auto distance = context.target_distances.GetDistance(stations[index]);
    if (not context.robbed.test(index) and distance < nearest) {
      nearest = distance;
      result = index;
    }
  }
  return result;
}

std::optional<std::size_t> NearestByManhattan::ChooseStation(const PolicyContext& context,
                                                             [[maybe_unused]] Random& random) const {
  auto result = std::optional<std::size_t>{};
  auto nearest = std::numeric_limits<int>::max();
  const auto cell = GetCell(context.target.position);
  const auto stations = context.road.GetStations();
  for (auto index = std::size_t{0}; index < stations.size(); ++index) {
    const auto distance = std::abs(stations[index].x - cell.x) + std::abs(stations[index].y - cell.y);
    if (not context.robbed.test(index) and distance <= nearest) {
      nearest = distance;
      result = index;
    }
  }
  return result;
}

std::optional<std::size_t> AwayFromPlayer::ChooseStation(const PolicyContext& context, Random& random) const {
  auto result = std::optional<std::size_t>{};
  auto nearest = RouteField::kUnreachable;
  const auto stations = context.road.GetStations();
  for (auto index = std::size_t{0}; index < stations.size(); ++index) {
    const auto distance = context.target_distances.GetDistance(stations[index]);
    if (not context.robbed.test(index) and distance < context.player_distances.GetDistance(stations[index]) and
        distance < nearest) {
      nearest = distance;
      result = index;
    }
  }
  // The player is nearer to every station left: the nearest all the same.
  return result ? result : NearestByRoad{}.ChooseStation(context, random);
}

bool LeaveWhenWatched::Rob(const PolicyContext& context, [[maybe_unused]] Random& random) const {
  return not IsWithinOneCell(GetCell(context.player.position), GetCell(context.target.position));
}

bool LeaveWhenWatched::Flee([[maybe_unused]] const PolicyContext& context, [[maybe_unused]] Random& random) const {
  return false;
}

bool LeaveWhenWatched::Shoot(const PolicyContext& context, [[maybe_unused]] Random& random) const {
  return context.arrest == ArrestMethod::kShoot and
         GetCell(context.player.position) == GetCell(context.target.position);
}

std::optional<TrafficSpawn> AheadOfPlayer::Spawn(const PolicyContext& context, Random& random) const {
  auto result = std::optional<TrafficSpawn>{};
  const auto cell = GetCell(context.player.position);
  const auto exits = context.road.GetExits(cell);
  // The cell ahead where the road goes on that way, else another next to the player's, never the
  // one behind.
  const auto facing = GetFacingSide(context.player.GetBodyHeading());
  const auto ahead =
      HasSide(exits, facing) ? std::optional{facing} : ChooseOtherSide(exits, facing, GetOpposite(facing), random);
  if (ahead) {
    // In by that cell's far side where the road comes from there, through the player's cell and
    // out of it straight on where the road allows.
    const auto spawn = GetNeighbor(cell, *ahead);
    const auto toward = GetOpposite(*ahead);
    const auto spawn_exits = context.road.GetExits(spawn);
    const auto entry = HasSide(spawn_exits, *ahead) ? ahead : ChooseOtherSide(spawn_exits, toward, toward, random);
    const auto exit = HasSide(exits, toward) ? std::optional{toward} : ChooseOtherSide(exits, *ahead, *ahead, random);
    if (entry and exit) {
      result = TrafficSpawn{.cell = spawn, .entry = *entry, .goal = GetNeighbor(cell, *exit)};
    }
  }
  return result;
}

Cell AheadOfPlayer::ChooseGoal(const PolicyContext& context, const Vehicle& car, Random& random) const {
  auto goal = GetCell(car.position);
  if (const auto stations = context.road.GetStations(); not stations.empty()) {
    const auto count = static_cast<std::uint32_t>(stations.size());
    const auto index = random.Below(count);
    goal = stations[index] == goal ? stations[(index + 1) % count] : stations[index];
  }
  return goal;
}

}  // namespace hp2
