#include "core/vehicle.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "core/road.hpp"
#include "core/road_map.hpp"
#include "core/world.hpp"

namespace hp2 {
namespace {

constexpr auto kTuning = VehicleTuning{
    .top_speed = 8000.0F,
    .top_reverse_speed = 2000.0F,
    .acceleration = 2000.0F,
    .braking = 4000.0F,
    .drag = 0.2F,
    .off_road_drag = 1.0F,
    .turning_radius = 700.0F,
    .grip = 3000.0F,
};
constexpr auto kTickSeconds = 1.0F / 60.0F;
constexpr auto kTicksPerSecond = 60;
// The middle of cell (10, 10), far enough from the map's edges for a few seconds' drive.
constexpr auto kStart = WorldPoint{.x = 10.5F * kCellUnits, .y = 10.5F * kCellUnits};

// Made-up maps of one cell type everywhere: type 1 with an outline over the whole cell, so all is
// road, or type 0, so all is desert.
class VehicleTest : public testing::Test {
 protected:
  VehicleTest() { ranges_[1] = IndexRange{.first = 0, .count = static_cast<std::uint32_t>(outline_.size())}; }

  [[nodiscard]] Road MakeRoad(std::uint8_t type) {
    cells_.fill(type);
    return Road{RoadMapView{.cells = cells_}, RoadShapes{.points = outline_, .cell_types = ranges_}, Scenery{}};
  }

  static void Run(Vehicle& vehicle, const Road& road, int ticks) {
    for (auto tick = 0; tick < ticks; ++tick) {
      Drive(vehicle, kTuning, road, kTickSeconds);
    }
  }

  std::array<std::uint8_t, RoadMapView::kSize * RoadMapView::kSize> cells_{};
  std::array<ShapePoint, 5> outline_{
      {{.x = 0, .y = 0}, {.x = 16384, .y = 0}, {.x = 16384, .y = 16384}, {.x = 0, .y = 16384}, {.x = 0, .y = 0}}};
  std::array<IndexRange, kRoadCellTypeCount> ranges_{};
};

TEST_F(VehicleTest, ReachesTopSpeedAndStaysThere) {
  const auto road = MakeRoad(1);
  auto car = Vehicle{.position = kStart, .controls = {.throttle = 1.0F}};
  Run(car, road, 5 * kTicksPerSecond);
  EXPECT_FLOAT_EQ(car.speed, kTuning.top_speed);
}

TEST_F(VehicleTest, BrakesToAStopThenReverses) {
  const auto road = MakeRoad(1);
  auto car = Vehicle{.position = kStart, .speed = 4000.0F, .controls = {.throttle = -1.0F}};
  Run(car, road, kTicksPerSecond);
  EXPECT_NEAR(car.speed, 0.0F, 1.0F);
  Run(car, road, 2 * kTicksPerSecond);
  EXPECT_FLOAT_EQ(car.speed, -kTuning.top_reverse_speed);
}

TEST_F(VehicleTest, CoastingSlowsWithoutStopping) {
  const auto road = MakeRoad(1);
  auto car = Vehicle{.position = kStart, .speed = 4000.0F};
  Run(car, road, kTicksPerSecond);
  EXPECT_LT(car.speed, 4000.0F);
  EXPECT_GT(car.speed, 3000.0F);
}

TEST_F(VehicleTest, TurnsLeftAtSpeedOverRadiusWhenSlow) {
  const auto road = MakeRoad(1);
  // At 700 units/s full lock asks for 1 rad/s, well within the grip.
  auto car = Vehicle{.position = kStart, .speed = 700.0F, .controls = {.steer = 1.0F}};
  Run(car, road, 1);
  EXPECT_NEAR(car.heading, 1.0F / kTicksPerSecond, 1e-4F);
}

TEST_F(VehicleTest, GripLimitsTheTurnWhenFast) {
  const auto road = MakeRoad(1);
  // At 6000 units/s full lock asks for 8.6 rad/s; the grip allows 3000 / 6000 = 0.5 rad/s.
  auto car = Vehicle{.position = kStart, .speed = 6000.0F, .controls = {.steer = 1.0F}};
  Run(car, road, 1);
  EXPECT_NEAR(car.heading, 0.5F / kTicksPerSecond, 1e-4F);
}

TEST_F(VehicleTest, LosesSpeedFasterOffTheRoad) {
  const auto paved = MakeRoad(1);
  auto on_road = Vehicle{.position = kStart, .speed = 4000.0F};
  Run(on_road, paved, kTicksPerSecond);
  const auto desert = MakeRoad(0);
  auto off_road = Vehicle{.position = kStart, .speed = 4000.0F};
  Run(off_road, desert, kTicksPerSecond);
  EXPECT_LT(off_road.speed, on_road.speed);
}

TEST(CarCondition, TheEngineIdlesInFirstWhileStopped) {
  auto condition = CarCondition{.temperature = 0.5F};
  condition.Update(Vehicle{}, 1.0F);
  EXPECT_EQ(condition.gear, 1);
  EXPECT_FLOAT_EQ(condition.rpm, 0.0F);
  EXPECT_FLOAT_EQ(condition.fuel, 1.0F);
  // It cools 100 of 65536 a frame, 20 frames a second.
  EXPECT_NEAR(condition.temperature, 0.5F - (2000.0F / 65536.0F), 1e-5F);
}

TEST(CarCondition, TheGearAndTheEngineFollowTheSpeed) {
  auto condition = CarCondition{};
  // 3000 units a second are 150 a frame of the original's: second gear, 130 + 70 * 15 / 8.
  condition.Update(Vehicle{.speed = 3000.0F}, kTickSeconds);
  EXPECT_EQ(condition.gear, 2);
  EXPECT_FLOAT_EQ(condition.rpm, 261.25F);
  // Each gear ends at a multiple of 80 a frame: 1600 units a second is still first.
  condition.Update(Vehicle{.speed = 1600.0F}, kTickSeconds);
  EXPECT_EQ(condition.gear, 1);
  EXPECT_FLOAT_EQ(condition.rpm, 240.0F);
  // Backward counts as forward: 100 a frame, second gear.
  condition.Update(Vehicle{.speed = -2000.0F}, kTickSeconds);
  EXPECT_EQ(condition.gear, 2);
  EXPECT_FLOAT_EQ(condition.rpm, 167.5F);
  // Flat out: fifth, 250 + 80 * 15 / 8.
  condition.Update(Vehicle{.speed = 8000.0F}, kTickSeconds);
  EXPECT_EQ(condition.gear, 5);
  EXPECT_FLOAT_EQ(condition.rpm, 400.0F);
}

TEST(CarCondition, FlatOutBurnsFuelAndHeatsTheEngine) {
  auto condition = CarCondition{};
  for (auto tick = 0; tick < kTicksPerSecond; ++tick) {
    condition.Update(Vehicle{.speed = 8000.0F}, kTickSeconds);
  }
  // A second at 400 a frame: 20 frames of (400 / 8)^2 / 64 = 39 of the tank's 65536, and of 100
  // of the temperature's.
  EXPECT_NEAR(condition.fuel, 1.0F - (20.0F * 39.0625F / 65536.0F), 1e-5F);
  EXPECT_NEAR(condition.temperature, 2000.0F / 65536.0F, 1e-5F);
}

TEST(CarCondition, TheTankAndTheTemperatureStopAtTheirEnds) {
  auto condition = CarCondition{.fuel = 0.0001F, .temperature = 0.9999F};
  condition.Update(Vehicle{.speed = 8000.0F}, 1.0F);
  EXPECT_FLOAT_EQ(condition.fuel, 0.0F);
  EXPECT_FLOAT_EQ(condition.temperature, 1.0F);
}

TEST_F(VehicleTest, MovesAlongItsHeading) {
  const auto road = MakeRoad(1);
  auto car = Vehicle{.position = kStart, .heading = std::numbers::pi_v<float> / 2.0F, .speed = 1000.0F};
  Run(car, road, 1);
  EXPECT_NEAR(car.position.x, kStart.x, 0.01F);
  EXPECT_GT(car.position.y, kStart.y);
}

}  // namespace
}  // namespace hp2
