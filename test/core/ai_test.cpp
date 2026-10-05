#include "core/ai.hpp"

#include <gtest/gtest.h>

#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "assets.hpp"
#include "core/game_state.hpp"
#include "core/random.hpp"
#include "core/road.hpp"
#include "core/vehicle.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

constexpr auto kTickSeconds = 1.0F / 60.0F;
constexpr auto kTicksPerSecond = 60;
// The fastest criminal: 400 units a frame.
constexpr auto kTuning = VehicleTuning{
    .top_speed = 8000.0F,
    .acceleration = 1500.0F,
    .braking = 4000.0F,
    .drag = 0.2F,
    .off_road_drag = 1.0F,
    .turning_radius = 700.0F,
    .grip = 3000.0F,
};
constexpr auto kCrossroads = Cell{.x = 25, .y = 5};
constexpr auto kFarStation = Cell{.x = 25, .y = 38};
constexpr auto kNearStation = Cell{.x = 32, .y = 5};

class AiTest : public testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
  }
  [[nodiscard]] Road MakeRoad() const {
    const auto& assets = manager_.Engine();
    return Road{assets.road_map, assets.road_shapes, assets.scenery};
  }
  // A car on the lane that enters `cell` from the north.
  [[nodiscard]] static Vehicle PlaceCar(Cell cell) {
    return Vehicle{.position = GetCellOrigin(cell) + WorldPoint{.x = 7936.0F, .y = 16000.0F},
                   .heading = -kFullTurn / 4.0F};
  }
  host::AssetManager manager_;
  // Long-lived, as in a mission: a driver holds its route field.
  AiDriver driver_;
  RouteField target_distances_;
  RouteField player_distances_;
};

TEST_F(AiTest, ARouteFieldCountsTheCellsToItsGoal) {
  const auto road = MakeRoad();
  auto& route = driver_.route;
  route.Build(road, kNearStation);
  EXPECT_EQ(route.GetGoal(), kNearStation);
  EXPECT_EQ(route.GetDistance(kNearStation), 0);
  // A north-south station: its neighbors by road lie north and south.
  EXPECT_EQ(route.GetDistance(Cell{.x = 32, .y = 6}), 1);
  EXPECT_EQ(route.GetDistance(Cell{.x = 32, .y = 4}), 1);
  EXPECT_EQ(route.GetDistance(Cell{.x = 0, .y = 0}), RouteField::kUnreachable);
  EXPECT_EQ(route.GetDistance(Cell{.x = -1, .y = 70}), RouteField::kUnreachable);
  EXPECT_FALSE(route.ChooseExit(road, kNearStation, Side::kSouth));
}

TEST_F(AiTest, EveryRoadLeadsToTheGoal) {
  const auto road = MakeRoad();
  auto& route = driver_.route;
  route.Build(road, kFarStation);
  for (auto row = std::int16_t{0}; row < 40; ++row) {
    for (auto column = std::int16_t{0}; column < 40; ++column) {
      for (const auto side : kSides) {
        auto cell = Cell{.x = column, .y = row};
        if (HasSide(road.GetExits(cell), side)) {
          // Never back the way it came, so the way may be longer than the distance says.
          auto entry = side;
          auto steps = 0;
          while (cell != kFarStation and steps < 300) {
            const auto exit = route.ChooseExit(road, cell, entry);
            ASSERT_TRUE(exit);
            ASSERT_NE(*exit, entry);
            cell = GetNeighbor(cell, *exit);
            entry = GetOpposite(*exit);
            ++steps;
          }
          ASSERT_EQ(cell, kFarStation) << "from " << column << "," << row;
        }
      }
    }
  }
}

TEST_F(AiTest, ADriverKeepsToItsLaneAllTheWayToItsGoal) {
  const auto road = MakeRoad();
  driver_.cruise_speed = kTuning.top_speed;
  driver_.route.Build(road, kFarStation);
  auto car = PlaceCar(kCrossroads);
  PlanLanes(driver_, road, car.position, true);
  auto top_speed = 0.0F;
  auto widest = 0.0F;
  auto ticks = 0;
  while (driver_.cell != kFarStation and ticks < 10 * 60 * kTicksPerSecond) {
    car.controls = FollowLane(driver_, car, kTuning);
    Drive(car, kTuning, road, kTickSeconds);
    if (GetCell(car.position) != driver_.cell) {
      PlanLanes(driver_, road, car.position, true);
    }
    ASSERT_TRUE(road.IsOnRoad(car.position)) << "tick " << ticks << " in " << driver_.cell.x << "," << driver_.cell.y;
    top_speed = std::max(top_speed, car.speed);
    widest = std::max(widest, std::abs(driver_.lanes[0].Locate(car.position).offset));
    ++ticks;
  }
  EXPECT_EQ(driver_.cell, kFarStation);
  EXPECT_GT(top_speed, 0.9F * kTuning.top_speed);
  // Half a lane at most off its middle.
  EXPECT_LT(widest, 128.0F);
}

TEST_F(AiTest, ADriverPullingInStopsAtThePumps) {
  const auto road = MakeRoad();
  driver_.cruise_speed = kTuning.top_speed;
  driver_.route.Build(road, kNearStation);
  auto car = PlaceCar(kCrossroads);
  PlanLanes(driver_, road, car.position, true);
  auto ticks = 0;
  auto stopped = false;
  while (not stopped and ticks < 5 * 60 * kTicksPerSecond) {
    car.controls = FollowLane(driver_, car, kTuning);
    Drive(car, kTuning, road, kTickSeconds);
    if (GetCell(car.position) != driver_.cell) {
      PlanLanes(driver_, road, car.position, true);
    }
    if (driver_.cell == kNearStation) {
      driver_.plan = AiDriver::Plan::kPullIn;
    }
    ASSERT_TRUE(road.IsOnRoad(car.position)) << "tick " << ticks;
    ASSERT_GE(car.speed, 0.0F);
    stopped = driver_.plan == AiDriver::Plan::kPullIn and car.speed == 0.0F;
    ++ticks;
  }
  ASSERT_TRUE(stopped);
  EXPECT_TRUE(road.IsInStationArea(car.position));
  const auto& driveway = driver_.lanes[0];
  EXPECT_NEAR(driveway.Locate(car.position).distance, driveway.GetLength() / 2.0F, 100.0F);
  // It stays there.
  for (auto tick = 0; tick < kTicksPerSecond; ++tick) {
    car.controls = FollowLane(driver_, car, kTuning);
    Drive(car, kTuning, road, kTickSeconds);
  }
  EXPECT_EQ(car.speed, 0.0F);
}

TEST_F(AiTest, TheCriminalPicksItsStationByThePolicy) {
  const auto road = MakeRoad();
  auto random = Random{1};
  auto robbed = std::bitset<kStationCount>{};
  const auto player = PlaceCar(Cell{.x = 30, .y = 7});
  const auto target = PlaceCar(kCrossroads);
  target_distances_.Build(road, GetCell(target.position));
  player_distances_.Build(road, GetCell(player.position));
  const auto context = PolicyContext{.road = road,
                                     .arrest = ArrestMethod::kPullOver,
                                     .player = player,
                                     .target = target,
                                     .robbed = robbed,
                                     .target_distances = target_distances_,
                                     .player_distances = player_distances_};
  const auto stations = road.GetStations();
  const auto by_road = NearestByRoad{}.ChooseStation(context, random);
  ASSERT_TRUE(by_road);
  for (const auto station : stations) {
    EXPECT_GE(target_distances_.GetDistance(station), target_distances_.GetDistance(stations[*by_road]));
  }
  // From (25, 5): (23, 6) is 3 cells away as the crow flies, nearer than any other.
  const auto by_manhattan = NearestByManhattan{}.ChooseStation(context, random);
  ASSERT_TRUE(by_manhattan);
  EXPECT_EQ(stations[*by_manhattan], (Cell{.x = 23, .y = 6}));
  const auto away = AwayFromPlayer{}.ChooseStation(context, random);
  ASSERT_TRUE(away);
  EXPECT_LT(target_distances_.GetDistance(stations[*away]), player_distances_.GetDistance(stations[*away]));
  // With every station robbed there is none to choose.
  robbed.set();
  EXPECT_FALSE(NearestByRoad{}.ChooseStation(context, random));
  EXPECT_FALSE(NearestByManhattan{}.ChooseStation(context, random));
  EXPECT_FALSE(AwayFromPlayer{}.ChooseStation(context, random));
}

TEST_F(AiTest, TheCriminalRobsOnlyUnwatched) {
  const auto road = MakeRoad();
  auto random = Random{1};
  const auto robbed = std::bitset<kStationCount>{};
  const auto target = PlaceCar(kNearStation);
  auto player = PlaceCar(Cell{.x = 32, .y = 6});
  const auto context = PolicyContext{.road = road,
                                     .arrest = ArrestMethod::kShoot,
                                     .player = player,
                                     .target = target,
                                     .robbed = robbed,
                                     .target_distances = target_distances_,
                                     .player_distances = player_distances_};
  EXPECT_FALSE(LeaveWhenWatched{}.Rob(context, random));
  EXPECT_FALSE(LeaveWhenWatched{}.Shoot(context, random));
  player = PlaceCar(kNearStation);
  EXPECT_TRUE(LeaveWhenWatched{}.Shoot(context, random));
  player = PlaceCar(Cell{.x = 32, .y = 8});
  EXPECT_TRUE(LeaveWhenWatched{}.Rob(context, random));
  EXPECT_FALSE(LeaveWhenWatched{}.Flee(context, random));
}

}  // namespace
}  // namespace hp2
