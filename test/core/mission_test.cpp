#include "core/mission.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include "assets.hpp"
#include "core/ai.hpp"
#include "core/game_state.hpp"
#include "core/road.hpp"
#include "core/road_map.hpp"
#include "core/vehicle.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class MissionTest : public testing::Test {
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
  host::AssetManager manager_;
};

TEST_F(MissionTest, StartsTheCarsAtTheOriginalsPlaces) {
  auto mission = Mission{MakeRoad()};
  for (auto seed = std::uint64_t{0}; seed < 32; ++seed) {
    mission.Start(kMissions[0], AiPolicies{}, seed);
    const auto& road = mission.GetRoad();
    const auto player = mission.GetPlayer().position;
    const auto cell = GetCell(player);
    EXPECT_TRUE(cell == (Cell{.x = 11, .y = 15}) or cell == (Cell{.x = 24, .y = 11}) or
                cell == (Cell{.x = 25, .y = 28}))
        << "seed " << seed;
    EXPECT_TRUE(road.IsOnRoad(player)) << "seed " << seed;
    // Every start of the criminal is a crossroads.
    EXPECT_EQ(road.GetCellType(GetCell(mission.GetTarget().position)), 10) << "seed " << seed;
  }
  EXPECT_EQ(mission.GetProgress().bounty, 2000);
}

TEST_F(MissionTest, TheSameSeedStartsTheSameMission) {
  auto first = Mission{MakeRoad()};
  auto second = Mission{MakeRoad()};
  first.Start(kMissions[2], AiPolicies{}, 1234);
  second.Start(kMissions[2], AiPolicies{}, 1234);
  EXPECT_EQ(first.GetPlayer().position.x, second.GetPlayer().position.x);
  EXPECT_EQ(first.GetPlayer().position.y, second.GetPlayer().position.y);
  EXPECT_EQ(first.GetTarget().position.x, second.GetTarget().position.x);
  EXPECT_EQ(first.GetTarget().position.y, second.GetTarget().position.y);
}

TEST_F(MissionTest, TheCriminalRobsStationAfterStation) {
  constexpr auto kTenMinutes = 10 * 60 * 60;
  auto mission = Mission{MakeRoad()};
  for (auto seed = std::uint64_t{0}; seed < 4; ++seed) {
    mission.Start(kMissions[5], AiPolicies{}, seed);
    const auto& road = mission.GetRoad();
    auto ticks = 0;
    // The player stays at the start, more than a cell from every station.
    while (mission.GetProgress().robbed.count() < 3 and ticks < kTenMinutes) {
      mission.Step(PlayerCommands{});
      ASSERT_TRUE(road.IsOnRoad(mission.GetTarget().position)) << "seed " << seed << " tick " << ticks;
      ++ticks;
    }
    EXPECT_EQ(mission.GetProgress().robbed.count(), 3U) << "seed " << seed;
    // The third was robbed this tick, from its pumps.
    const auto station = road.GetStationIndex(GetCell(mission.GetTarget().position));
    ASSERT_TRUE(station) << "seed " << seed;
    EXPECT_TRUE(mission.GetProgress().robbed.test(*station));
    EXPECT_TRUE(road.IsInStationArea(mission.GetTarget().position));
  }
}

TEST_F(MissionTest, ATrafficCarComesTowardThePlayerAndPasses) {
  auto mission = Mission{MakeRoad()};
  for (auto seed = std::uint64_t{0}; seed < 4; ++seed) {
    mission.Start(kMissions[0], AiPolicies{}, seed);
    const auto& road = mission.GetRoad();
    const auto player_cell = GetCell(mission.GetPlayer().position);
    mission.Step(PlayerCommands{});
    // It appears in a cell next to the player's.
    ASSERT_EQ(mission.GetTraffic().size(), 1U) << "seed " << seed;
    const auto first_cell = GetCell(mission.GetTraffic().front().position);
    EXPECT_EQ(std::abs(first_cell.x - player_cell.x) + std::abs(first_cell.y - player_cell.y), 1);
    auto passed = false;
    for (auto tick = 0; tick < 60 * 60; ++tick) {
      mission.Step(PlayerCommands{});
      ASSERT_LE(mission.GetTraffic().size(), 1U);
      for (const auto& car : mission.GetTraffic()) {
        ASSERT_TRUE(road.IsOnRoad(car.position)) << "seed " << seed << " tick " << tick;
        EXPECT_GE(car.color_scheme, 1);
        EXPECT_LE(car.color_scheme, 7);
        const auto cell = GetCell(car.position);
        EXPECT_LE(std::abs(cell.x - player_cell.x), 3);
        EXPECT_LE(std::abs(cell.y - player_cell.y), 3);
        passed = passed or cell == player_cell;
      }
    }
    EXPECT_TRUE(passed) << "seed " << seed;
  }
}

TEST_F(MissionTest, ThePlayerStopsAtAStationAndLeavesServed) {
  // The player's car, for a driver of its own that takes it to the nearest station's driveway.
  constexpr auto kTuning = VehicleTuning{
      .top_speed = 8000.0F, .acceleration = 2000.0F, .braking = 4000.0F, .turning_radius = 700.0F, .grip = 3000.0F};
  auto mission = Mission{MakeRoad()};
  mission.Start(kMissions[0], AiPolicies{}, 0);
  const auto& road = mission.GetRoad();
  const auto& player = mission.GetPlayer();
  auto driver = AiDriver{.cruise_speed = 4000.0F};
  auto& route = driver.route;
  route.Build(road, GetCell(player.position));
  auto station = std::size_t{0};
  const auto stations = road.GetStations();
  for (auto index = std::size_t{1}; index < stations.size(); ++index) {
    if (route.GetDistance(stations[index]) < route.GetDistance(stations[station])) {
      station = index;
    }
  }
  route.Build(road, stations[station]);
  PlanLanes(driver, road, player.position, true);
  auto stopped = false;
  for (auto tick = 0; tick < 5 * 60 * 60 and not stopped; ++tick) {
    if (GetCell(player.position) != driver.cell) {
      PlanLanes(driver, road, player.position, true);
    }
    const auto events = mission.Step(PlayerCommands{.controls = FollowLane(driver, player, kTuning)});
    stopped = events.test(std::to_underlying(MissionEvent::kStationStop));
  }
  ASSERT_TRUE(stopped);
  EXPECT_EQ(mission.GetStation(), station);
  EXPECT_EQ(player.speed, 0.0F);
  EXPECT_TRUE(road.IsInStationArea(player.position));
  EXPECT_LT(mission.GetCondition().fuel, 1.0F);

  mission.LeaveStation(1.0F, GameState::kFullTires);
  EXPECT_FALSE(mission.GetStation());
  EXPECT_FLOAT_EQ(mission.GetCondition().fuel, 1.0F);
  EXPECT_EQ(GetCell(player.position), stations[station]);
  EXPECT_TRUE(road.IsOnRoad(player.position));
  EXPECT_FALSE(road.IsInStationArea(player.position));
  EXPECT_FALSE(mission.Step(PlayerCommands{}).test(std::to_underlying(MissionEvent::kStationStop)));
}

TEST(MissionRules, APullOverArrestNeedsTheCriminalCloseAhead) {
  const auto middle = WorldPoint{.x = 10.5F * kCellUnits, .y = 10.5F * kCellUnits};
  const auto north = kFullTurn / 4.0F;
  const auto player = Vehicle{.position = middle, .heading = north};
  const auto place = [&](float east, float ahead, float heading) {
    return Vehicle{.position = middle + WorldPoint{.x = east, .y = ahead}, .heading = heading};
  };
  EXPECT_TRUE(IsInArrestReach(player, place(0.0F, 500.0F, north)));
  EXPECT_TRUE(IsInArrestReach(player, place(200.0F, 500.0F, north + 1.0F)));
  EXPECT_FALSE(IsInArrestReach(player, place(0.0F, 1200.0F, north)));   // too far
  EXPECT_FALSE(IsInArrestReach(player, place(0.0F, -500.0F, north)));   // behind
  EXPECT_FALSE(IsInArrestReach(player, place(400.0F, 500.0F, north)));  // out of the view
  EXPECT_FALSE(IsInArrestReach(player, place(0.0F, 500.0F, -north)));   // coming the other way
  // Close ahead, but across the cell's edge.
  const auto at_edge = Vehicle{.position = middle + WorldPoint{.y = (0.5F * kCellUnits) - 100.0F}, .heading = north};
  const auto beyond = Vehicle{.position = at_edge.position + WorldPoint{.y = 500.0F}, .heading = north};
  EXPECT_FALSE(IsInArrestReach(at_edge, beyond));
}

// Made-up maps for the rules: one cell type everywhere, type 1 with an outline over the whole cell,
// so all is road, or type 0, so all is desert, and one station far from the starts, so there is
// one left to rob.
class MissionRulesTest : public testing::Test {
 protected:
  MissionRulesTest() {
    ranges_[1] = IndexRange{.first = 0, .count = static_cast<std::uint32_t>(outline_.size())};
    ranges_[11] = ranges_[1];
  }

  // With `object_type`, every cell of the map's type has one such object in its middle, on the way
  // of the player's car from any of its starts.
  [[nodiscard]] Road MakeRoad(std::uint8_t type, std::uint16_t object_type = 0) {
    cells_.fill(type);
    cells_[(50 * RoadMapView::kSize) + 50] = 11;
    auto scenery = Scenery{};
    if (object_type != 0) {
      objects_[0] = PlacedObject{.x = 8192, .y = 8192, .type = object_type};
      scenery.objects = objects_;
      scenery.cell_types[type] = IndexRange{.first = 0, .count = 1};
    }
    return Road{RoadMapView{.cells = cells_}, RoadShapes{.points = outline_, .cell_types = ranges_}, scenery};
  }

  // Starts `type` from the seed that has the player drive south down column 25 with the criminal's
  // car standing in it, straight ahead: on these maps the criminal has no station to go to.
  // Returns whether there is such a seed.
  static bool StartBehindTarget(Mission& mission, const MissionType& type) {
    auto found = false;
    for (auto seed = std::uint64_t{0}; seed < 2000 and not found; ++seed) {
      mission.Start(type, AiPolicies{}, seed);
      found = GetCell(mission.GetPlayer().position) == Cell{.x = 25, .y = 28} and
              GetCell(mission.GetTarget().position) == Cell{.x = 25, .y = 5};
    }
    return found;
  }

  // The player's car behind the criminal's, units.
  [[nodiscard]] static float GetGap(const Mission& mission) {
    return mission.GetPlayer().position.y - mission.GetTarget().position.y;
  }

  // From StartBehindTarget, drives the player's car toward the criminal's at 2000 units a second
  // and brakes it to a stop about `gap` units behind.
  static void StopBehindTarget(Mission& mission, float gap) {
    constexpr auto kSpeed = 2000.0F;
    constexpr auto kBraking = 4000.0F;
    auto stopped = false;
    for (auto tick = 0; tick < 300 * 60 and not stopped; ++tick) {
      const auto speed = mission.GetPlayer().speed;
      const auto braking = GetGap(mission) - (speed * speed / (2.0F * kBraking)) <= gap;
      stopped = braking and speed == 0.0F;
      const auto cruise = speed < kSpeed ? 1.0F : 0.0F;
      if (not stopped) {
        mission.Step(PlayerCommands{.controls = {.throttle = braking ? -1.0F : cruise}});
      }
    }
  }

  // Steps flat out until the player's car is in an impact of `kind`, for 10 seconds at most;
  // returns whether it came.
  static bool RunIntoImpact(Mission& mission, ImpactKind kind) {
    auto came = false;
    for (auto tick = 0; tick < 10 * 60 and not came; ++tick) {
      mission.Step(kFlatOut);
      came = mission.GetPlayer().impact.kind == kind;
    }
    return came;
  }

  // Steps with `commands` until `event`, for `seconds` at most; returns whether it came.
  static bool RunUntil(Mission& mission, const PlayerCommands& commands, MissionEvent event, int seconds) {
    auto came = false;
    for (auto tick = 0; tick < seconds * 60 and not came; ++tick) {
      came = mission.Step(commands).test(std::to_underlying(event));
    }
    return came;
  }

  static constexpr auto kFlatOut = PlayerCommands{.controls = {.throttle = 1.0F}};

  std::array<std::uint8_t, RoadMapView::kSize * RoadMapView::kSize> cells_{};
  std::array<ShapePoint, 5> outline_{
      {{.x = 0, .y = 0}, {.x = 16384, .y = 0}, {.x = 16384, .y = 16384}, {.x = 0, .y = 16384}, {.x = 0, .y = 0}}};
  std::array<IndexRange, kRoadCellTypeCount> ranges_{};
  std::array<PlacedObject, 1> objects_{};
};

TEST_F(MissionRulesTest, ACactusSpinsTheCarOutAndDamagesIt) {
  auto mission = Mission{MakeRoad(1, 1)};
  mission.Start(kMissions[0], AiPolicies{}, 1);
  // 6192 units from any start to the middle of its cell: the car gets there at about 5000 units a
  // second.
  ASSERT_TRUE(RunUntil(mission, kFlatOut, MissionEvent::kCrash, 10));
  EXPECT_EQ(mission.GetPlayer().impact.kind, ImpactKind::kSpin);
  // Twice its speed a frame, of 10000.
  EXPECT_NEAR(mission.GetCondition().damage, 0.05F, 0.01F);
  // It spins to a stop, whatever the throttle.
  auto ticks = 0;
  while (mission.GetPlayer().impact.kind == ImpactKind::kSpin and ticks < 3 * 60) {
    mission.Step(kFlatOut);
    ++ticks;
  }
  EXPECT_EQ(mission.GetPlayer().impact.kind, ImpactKind::kNone);
  EXPECT_EQ(mission.GetPlayer().speed, 0.0F);
  EXPECT_FALSE(mission.GetProgress().end_reason);
}

TEST_F(MissionRulesTest, StonesBumpTheCar) {
  auto mission = Mission{MakeRoad(1, 3)};
  mission.Start(kMissions[0], AiPolicies{}, 1);
  ASSERT_TRUE(RunIntoImpact(mission, ImpactKind::kBump));
  EXPECT_GT(mission.GetCondition().damage, 0.0F);
  EXPECT_LT(mission.GetCondition().damage, 0.04F);
  auto highest = 0.0F;
  auto crashed = false;
  for (auto tick = 0; tick < 60; ++tick) {
    crashed = crashed or mission.Step(kFlatOut).test(std::to_underlying(MissionEvent::kCrash));
    highest = std::max(highest, mission.GetPlayer().body_lift);
  }
  EXPECT_GT(highest, 30.0F);
  EXPECT_FALSE(crashed);
  EXPECT_EQ(mission.GetPlayer().impact.kind, ImpactKind::kNone);
}

TEST_F(MissionRulesTest, ABushScratchesAndDragsWithoutAnImpact) {
  auto mission = Mission{MakeRoad(1, 7)};
  mission.Start(kMissions[0], AiPolicies{}, 1);
  auto before = 0.0F;
  for (auto tick = 0; tick < 3 * 60 and mission.GetCondition().damage == 0.0F; ++tick) {
    before = mission.GetPlayer().speed;
    mission.Step(kFlatOut);
  }
  // In the bush the car takes damage and slows down in spite of the throttle.
  ASSERT_GT(mission.GetCondition().damage, 0.0F);
  EXPECT_GT(before, 4000.0F);
  EXPECT_LT(mission.GetPlayer().speed, before);
  EXPECT_EQ(mission.GetPlayer().impact.kind, ImpactKind::kNone);
}

TEST_F(MissionRulesTest, RammingTheCriminalCostsTheBounty) {
  auto mission = Mission{MakeRoad(1)};
  // Up to 5000 units a second, which the engine stands; returns the dollars the tick of the crash
  // took, 0 without a crash.
  const auto ram = [&mission]() {
    auto taken = 0;
    for (auto tick = 0; tick < 150 * 60 and taken == 0; ++tick) {
      const auto bounty = mission.GetProgress().bounty;
      const auto throttle = mission.GetPlayer().speed < 5000.0F ? 1.0F : 0.0F;
      const auto events = mission.Step(PlayerCommands{.controls = {.throttle = throttle}});
      if (events.test(std::to_underlying(MissionEvent::kCrash))) {
        taken = bounty - mission.GetProgress().bounty;
      }
    }
    return taken;
  };
  // A pull-over mission: 20 dollars a car and 20 for touching the criminal, a dollar more when the
  // bounty's drain falls on the same tick.
  ASSERT_TRUE(StartBehindTarget(mission, kMissions[0]));
  const auto taken = ram();
  EXPECT_GE(taken, 60);
  EXPECT_LE(taken, 61);
  EXPECT_EQ(mission.GetPlayer().impact.kind, ImpactKind::kSpin);
  EXPECT_EQ(mission.GetTarget().impact.kind, ImpactKind::kSpin);
  EXPECT_GT(mission.GetCondition().damage, 0.0F);
  // The criminal's car, which stood still, is knocked on.
  EXPECT_GT(mission.GetTarget().speed, 1000.0F);
  EXPECT_FALSE(mission.GetProgress().end_reason);
  // A roadblock mission: the 20 dollars a car, and no arrest for a player who was moving.
  ASSERT_TRUE(StartBehindTarget(mission, kMissions[2]));
  const auto roadblock_taken = ram();
  EXPECT_GE(roadblock_taken, 40);
  EXPECT_LE(roadblock_taken, 41);
  EXPECT_FALSE(mission.GetProgress().end_reason);
}

TEST_F(MissionRulesTest, FiveHitsArrestTheCriminalInAShootingMission) {
  auto mission = Mission{MakeRoad(1)};
  ASSERT_TRUE(StartBehindTarget(mission, kMissions[4]));
  // Stopped 2300 units behind the criminal's car, just outside its cell, where it does not fire.
  StopBehindTarget(mission, 2300.0F);
  EXPECT_NEAR(GetGap(mission), 2300.0F, 100.0F);
  ASSERT_FALSE(mission.GetProgress().end_reason);
  // Fire does nothing until the gun is out.
  EXPECT_FALSE(mission.Step(PlayerCommands{.firing = true}).any());
  mission.Step(PlayerCommands{.toggle_aim = true});
  ASSERT_TRUE(mission.GetProgress().aiming);
  auto shots = 0;
  auto hits = 0;
  auto over = false;
  for (auto tick = 0; tick < 60 and not over; ++tick) {
    const auto events = mission.Step(PlayerCommands{.firing = true});
    shots += events.test(std::to_underlying(MissionEvent::kShot)) ? 1 : 0;
    hits += events.test(std::to_underlying(MissionEvent::kTargetHit)) ? 1 : 0;
    over = events.test(std::to_underlying(MissionEvent::kMissionOver));
  }
  EXPECT_TRUE(over);
  EXPECT_EQ(shots, 5);
  EXPECT_EQ(hits, 5);
  EXPECT_EQ(mission.GetProgress().target_hits, 5);
  EXPECT_EQ(mission.GetProgress().end_reason, EndReason::kArrest);
  EXPECT_EQ(mission.GetProgress().hole_count, 0);
}

TEST_F(MissionRulesTest, ShootingTheCriminalCostsTheBountyInOtherMissions) {
  auto mission = Mission{MakeRoad(1)};
  ASSERT_TRUE(StartBehindTarget(mission, kMissions[0]));
  StopBehindTarget(mission, 2300.0F);
  mission.Step(PlayerCommands{.toggle_aim = true});
  const auto bounty = mission.GetProgress().bounty;
  auto hits = 0;
  for (auto tick = 0; tick < 30; ++tick) {
    hits += mission.Step(PlayerCommands{.firing = true}).test(std::to_underlying(MissionEvent::kTargetHit)) ? 1 : 0;
  }
  // 30 ticks: 10 shots, 20 dollars each, and a dollar or two of the bounty's own drain.
  EXPECT_EQ(hits, 10);
  EXPECT_GE(bounty - mission.GetProgress().bounty, 200);
  EXPECT_LE(bounty - mission.GetProgress().bounty, 202);
  EXPECT_FALSE(mission.GetProgress().end_reason);
  // Too far for the gun: the sight's line meets the ground first.
  ASSERT_TRUE(StartBehindTarget(mission, kMissions[0]));
  StopBehindTarget(mission, 4000.0F);
  mission.Step(PlayerCommands{.toggle_aim = true});
  auto events = MissionEvents{};
  for (auto tick = 0; tick < 30; ++tick) {
    events |= mission.Step(PlayerCommands{.firing = true});
  }
  EXPECT_TRUE(events.test(std::to_underlying(MissionEvent::kShot)));
  EXPECT_FALSE(events.test(std::to_underlying(MissionEvent::kTargetHit)));
}

TEST_F(MissionRulesTest, TheCriminalShootsADriverComingUpItsLine) {
  auto mission = Mission{MakeRoad(1)};
  ASSERT_TRUE(StartBehindTarget(mission, kMissions[4]));
  // Into the criminal's cell straight behind it: its first bullet comes through the middle of the
  // windshield.
  auto events = MissionEvents{};
  for (auto tick = 0; tick < 300 * 60 and not mission.GetProgress().end_reason; ++tick) {
    const auto throttle = mission.GetPlayer().speed < 2000.0F ? 1.0F : 0.0F;
    events |= mission.Step(PlayerCommands{.controls = {.throttle = throttle}});
  }
  EXPECT_EQ(mission.GetProgress().end_reason, EndReason::kShot);
  EXPECT_TRUE(events.test(std::to_underlying(MissionEvent::kWindshieldHit)));
  EXPECT_FALSE(events.test(std::to_underlying(MissionEvent::kCrash)));
  EXPECT_EQ(GetCell(mission.GetPlayer().position), GetCell(mission.GetTarget().position));
}

TEST_F(MissionRulesTest, NoTrafficWhileThePlayerIsCloseToTheCriminal) {
  auto mission = Mission{MakeRoad(1)};
  ASSERT_TRUE(StartBehindTarget(mission, kMissions[0]));
  mission.Step(PlayerCommands{});
  EXPECT_EQ(mission.GetTraffic().size(), 1U);
  // A cell behind the criminal: the traffic car there was goes once it is out of sight, and no
  // other comes.
  StopBehindTarget(mission, 2300.0F);
  for (auto tick = 0; tick < 10 * 60; ++tick) {
    mission.Step(PlayerCommands{});
  }
  for (auto tick = 0; tick < 10 * 60; ++tick) {
    mission.Step(PlayerCommands{});
    ASSERT_TRUE(mission.GetTraffic().empty()) << "tick " << tick;
  }
}

TEST_F(MissionRulesTest, TheCriminalRollsToAStopWithThePlayerOnceTheMissionIsOver) {
  auto mission = Mission{MakeRoad(1)};
  mission.Start(MissionType{.arrest = ArrestMethod::kPullOver, .target_max_speed = 200, .bounty = 10}, AiPolicies{}, 1);
  ASSERT_TRUE(RunUntil(mission, kFlatOut, MissionEvent::kMissionOver, 5));
  mission.Step(kFlatOut);
  EXPECT_GT(mission.GetPlayer().speed, 4000.0F);
  EXPECT_FLOAT_EQ(mission.GetTarget().speed, mission.GetPlayer().speed);
  RunUntil(mission, kFlatOut, MissionEvent::kMissionOver, 10);
  EXPECT_EQ(mission.GetPlayer().speed, 0.0F);
  EXPECT_EQ(mission.GetTarget().speed, 0.0F);
}

TEST_F(MissionRulesTest, TheBountyDrainsADollarEveryEighteenTicks) {
  auto mission = Mission{MakeRoad(1)};
  mission.Start(kMissions[0], AiPolicies{}, 1);
  EXPECT_FALSE(RunUntil(mission, PlayerCommands{}, MissionEvent::kMissionOver, 10));
  EXPECT_EQ(mission.GetProgress().bounty, 2000 - 33);
  EXPECT_FALSE(mission.GetProgress().end_reason);
}

TEST_F(MissionRulesTest, TheMissionEndsWhenTheBountyIsGone) {
  auto mission = Mission{MakeRoad(1)};
  mission.Start(MissionType{.arrest = ArrestMethod::kPullOver, .target_max_speed = 200, .bounty = 2}, AiPolicies{}, 1);
  EXPECT_TRUE(RunUntil(mission, PlayerCommands{}, MissionEvent::kMissionOver, 1));
  EXPECT_EQ(mission.GetTick(), 36U);
  EXPECT_EQ(mission.GetProgress().end_reason, EndReason::kBountyGone);
  // Over is over: no second event, and no more driving.
  EXPECT_FALSE(RunUntil(mission, kFlatOut, MissionEvent::kMissionOver, 2));
  EXPECT_EQ(mission.GetPlayer().speed, 0.0F);
}

TEST_F(MissionRulesTest, FlatOutOverheatsTheEngineAndTheCarRollsToAStop) {
  auto mission = Mission{MakeRoad(1)};
  mission.Start(kMissions[0], AiPolicies{}, 1);
  ASSERT_TRUE(RunUntil(mission, kFlatOut, MissionEvent::kMissionOver, 60));
  EXPECT_EQ(mission.GetProgress().end_reason, EndReason::kOverheated);
  EXPECT_GT(mission.GetPlayer().speed, 7000.0F);
  RunUntil(mission, kFlatOut, MissionEvent::kMissionOver, 10);
  EXPECT_EQ(mission.GetPlayer().speed, 0.0F);
}

TEST_F(MissionRulesTest, TooLongOffTheRoadCostsATireThenTheMission) {
  auto mission = Mission{MakeRoad(0)};
  mission.Start(kMissions[0], AiPolicies{}, 1);
  // The first tire: the car shakes to a stop and the mission goes on.
  ASSERT_TRUE(RunUntil(mission, kFlatOut, MissionEvent::kCrash, 30));
  EXPECT_EQ(mission.GetCondition().tires, 1);
  EXPECT_GT(mission.GetTick(), 15U * 60U);
  ASSERT_TRUE(RunUntil(mission, kFlatOut, MissionEvent::kSpinOut, 10));
  EXPECT_EQ(mission.GetPlayer().speed, 0.0F);
  EXPECT_GT(mission.GetCondition().damage, 0.0F);
  EXPECT_FALSE(mission.GetProgress().end_reason);
  // The second ends it.
  ASSERT_TRUE(RunUntil(mission, kFlatOut, MissionEvent::kMissionOver, 30));
  EXPECT_EQ(mission.GetCondition().tires, 0);
  EXPECT_EQ(mission.GetProgress().end_reason, EndReason::kTiresGone);
}

TEST_F(MissionTest, TheSameSeedReplaysTheSameMission) {
  auto first = Mission{MakeRoad()};
  auto second = Mission{MakeRoad()};
  first.Start(kMissions[4], AiPolicies{}, 99);
  second.Start(kMissions[4], AiPolicies{}, 99);
  const auto commands = PlayerCommands{.controls = {.steer = 0.1F, .throttle = 1.0F}};
  // Twenty seconds: not long enough for that driving to end the mission.
  for (auto tick = 0; tick < 20 * 60; ++tick) {
    first.Step(commands);
    second.Step(commands);
  }
  ASSERT_FALSE(first.GetProgress().end_reason);
  EXPECT_EQ(first.GetPlayer().position.x, second.GetPlayer().position.x);
  EXPECT_EQ(first.GetTarget().position.x, second.GetTarget().position.x);
  EXPECT_EQ(first.GetTarget().position.y, second.GetTarget().position.y);
  EXPECT_NE(first.GetTarget().speed, 0.0F);
}

}  // namespace
}  // namespace hp2
