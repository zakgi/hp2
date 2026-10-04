#pragma once

#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include "core/game_state.hpp"
#include "core/random.hpp"
#include "core/road.hpp"
#include "core/vehicle.hpp"
#include "core/world.hpp"

namespace hp2 {

// Distances in cells to one goal over the road, for choosing exits: a car leaves its cell by the
// open side whose neighbor is nearer the goal. The original chooses each exit greedily from the
// signs of the cell differences (PlanCellRoute, 0:5a44), which is not guaranteed to reach the
// goal; whether it fails on this map is unverified.
class RouteField {
 public:
  static constexpr auto kUnreachable = std::uint16_t{0xffff};

  // Distances from every road cell to `goal`, through open sides only.
  void Build(const Road& road, Cell goal);

  [[nodiscard]] Cell GetGoal() const { return goal_; }
  [[nodiscard]] std::uint16_t GetDistance(Cell cell) const;
  // The side of `cell` to leave by toward the goal; back through `entry` only when there is no
  // other way. None in the goal cell or off the road.
  [[nodiscard]] std::optional<Side> ChooseExit(const Road& road, Cell cell, Side entry) const;

 private:
  std::array<std::uint16_t, RoadMapView::kSize * RoadMapView::kSize> distances_{};
  Cell goal_;
};

// A computer driver: its goal, its way through the current cell and the next, and its pace
// (TargetCarAI 0:3f26, TrafficCarAI 0:3f62).
struct AiDriver {
  enum class Plan : std::uint8_t {
    kCruise,   // toward the goal cell
    kPullIn,   // into a station's driveway, stopping at the pumps
    kStopped,  // at the pumps: robbing, or waiting for the player to leave
  };

  Plan plan{Plan::kCruise};
  RouteField route;
  // The cell the car is in, and the side it came in by.
  Cell cell;
  Side entry{Side::kSouth};
  // The lanes through this cell and the next one on the route, so the point the driver chases
  // carries over from one to the other.
  std::array<Lane, 2> lanes{};
  float cruise_speed{};  // units per second
  float plan_seconds{};  // in the current plan
};

// The controls that keep `vehicle` on `driver`'s lanes: steer toward a point ahead, and hold a
// speed set by the cruise speed, the curve ahead and the plan (FollowCellPath 0:5c56 and
// SteerToward 0:5e92, reworked).
[[nodiscard]] Controls FollowLane(const AiDriver& driver, const Vehicle& vehicle);

// The decisions a mission can swap: the criminal's choice of station and its reactions to the
// player, where traffic appears and where it heads. Each kind is a concept, and a mission runs with
// one policy of each kind (AiPolicies). Policies hold no state and draw random numbers only from
// the mission's generator, so a mission still replays from its seed.

// What every policy may look at.
struct PolicyContext {
  const Road& road;
  ArrestMethod arrest;
  const Vehicle& player;
  const Vehicle& target;  // the criminal's car
  const std::bitset<kStationCount>& robbed;
  // Distances over the road from the criminal's cell and from the player's.
  const RouteField& target_distances;
  const RouteField& player_distances;
};

// The criminal's next station: an index into Road::GetStations(), none when every one is robbed.
template <typename T>
concept StationPolicy = requires(const T& policy, const PolicyContext& context, Random& random) {
  { policy.ChooseStation(context, random) } -> std::same_as<std::optional<std::size_t>>;
};

// The nearest by road.
struct NearestByRoad {
  [[nodiscard]] std::optional<std::size_t> ChooseStation(const PolicyContext& context, Random& random) const;
};
// The original: the nearest by Manhattan distance in cells, ignoring the roads; on a tie the last
// in the table (FindNearestStation, 0:334c).
struct NearestByManhattan {
  [[nodiscard]] std::optional<std::size_t> ChooseStation(const PolicyContext& context, Random& random) const;
};
// The nearest by road among those nearer the criminal than the player.
struct AwayFromPlayer {
  [[nodiscard]] std::optional<std::size_t> ChooseStation(const PolicyContext& context, Random& random) const;
};

// What the criminal does about the player.
template <typename T>
concept ReactionPolicy = requires(const T& policy, const PolicyContext& context, Random& random) {
  // Stopped at a station: rob it, or leave for another.
  { policy.Rob(context, random) } -> std::same_as<bool>;
  // Drop the current station and choose again, as the player closes in.
  { policy.Flee(context, random) } -> std::same_as<bool>;
  // Fire at the player; asked once per shot interval.
  { policy.Shoot(context, random) } -> std::same_as<bool>;
};

// The original: robs unless the player is within one cell (0:30ce), never flees, fires in
// shooting missions while the player is in its cell (SuspectShoot, 0:50b6).
struct LeaveWhenWatched {
  [[nodiscard]] bool Rob(const PolicyContext& context, Random& random) const;
  [[nodiscard]] bool Flee(const PolicyContext& context, Random& random) const;
  [[nodiscard]] bool Shoot(const PolicyContext& context, Random& random) const;
};

// Where a traffic car appears, and where it heads.
struct TrafficSpawn {
  Cell cell;
  Side entry{Side::kSouth};
  Cell goal;
};

template <typename T>
concept TrafficPolicy = requires(const T& policy, const PolicyContext& context, const Vehicle& car, Random& random) {
  // A new car, or none for now.
  { policy.Spawn(context, random) } -> std::same_as<std::optional<TrafficSpawn>>;
  // A new goal for `car`, which reached its last one.
  { policy.ChooseGoal(context, car, random) } -> std::same_as<Cell>;
};

// The original (UpdateTrafficCar, 0:540e): in an open neighbor of the player's cell, usually ahead,
// heading for the cell mirrored past the player.
struct AheadOfPlayer {
  [[nodiscard]] std::optional<TrafficSpawn> Spawn(const PolicyContext& context, Random& random) const;
  [[nodiscard]] Cell ChooseGoal(const PolicyContext& context, const Vehicle& car, Random& random) const;
};

// The policies a mission runs with, chosen by the Highway component; the first of each kind is the
// default.
struct AiPolicies {
  std::variant<NearestByRoad, NearestByManhattan, AwayFromPlayer> station;
  std::variant<LeaveWhenWatched> reaction;
  std::variant<AheadOfPlayer> traffic;
};

static_assert(StationPolicy<NearestByRoad> and StationPolicy<NearestByManhattan> and StationPolicy<AwayFromPlayer>);
static_assert(ReactionPolicy<LeaveWhenWatched>);
static_assert(TrafficPolicy<AheadOfPlayer>);

}  // namespace hp2
