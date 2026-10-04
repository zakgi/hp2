#include "core/mission.hpp"

#include <array>
#include <cstdint>

namespace hp2 {

namespace {

// The player's car (docs/highway.md, "Handling"); provisional values, to be tuned by feel.
constexpr auto kPlayerCar = VehicleTuning{
    .top_speed = 8000.0F,          // 400 a frame at 20 frames a second: 62 m/s
    .top_reverse_speed = 2000.0F,  // 100 a frame
    .acceleration = 2000.0F,       // 0 to top speed in 4 s
    .braking = 4000.0F,            // top speed to a stop in 2 s
    .drag = 0.2F,
    .off_road_drag = 1.0F,
    .turning_radius = 700.0F,  // about 5.5 m at full lock
    .grip = 3000.0F,           // about 2.4 g: the curves (radius 8192) at up to about 5000 units/s
};

// A car's place at the start of a mission, as the original's tables give it: a cell, the position
// inside it and a heading in half degrees.
struct StartPlace {
  Cell cell;
  std::int16_t east;
  std::int16_t north;
  std::int16_t heading;
};

// The player's three starts (0:b76e). The original picks one with two bits of the beam position
// and gives the fourth value to the first start; here each is as likely.
constexpr auto kPlayerStarts = std::to_array<StartPlace>({
    {.cell = {.x = 11, .y = 15}, .east = 0x07d0, .north = 0x2000, .heading = 0},
    {.cell = {.x = 24, .y = 11}, .east = 0x2000, .north = 0x07d0, .heading = 0xb4},
    {.cell = {.x = 25, .y = 28}, .east = 0x2000, .north = 0x3830, .heading = 0x21c},
});

// The criminal's 16 starts (1:236e), all crossroads, at the same place in the cell, facing south.
constexpr auto kTargetStartCells = std::to_array<Cell>({
    {.x = 25, .y = 5},
    {.x = 3, .y = 6},
    {.x = 12, .y = 6},
    {.x = 27, .y = 6},
    {.x = 30, .y = 7},
    {.x = 16, .y = 8},
    {.x = 33, .y = 14},
    {.x = 30, .y = 20},
    {.x = 21, .y = 21},
    {.x = 12, .y = 24},
    {.x = 16, .y = 26},
    {.x = 35, .y = 27},
    {.x = 10, .y = 29},
    {.x = 31, .y = 30},
    {.x = 30, .y = 32},
    {.x = 14, .y = 35},
});
constexpr auto kTargetStartEast = std::int16_t{0x2000};
constexpr auto kTargetStartNorth = std::int16_t{0x3830};
constexpr auto kTargetStartHeading = std::int16_t{0x21c};

Vehicle PlaceVehicle(Cell cell, std::int16_t east, std::int16_t north, std::int16_t heading) {
  return Vehicle{
      .position = GetCellOrigin(cell) + WorldPoint{.x = static_cast<float>(east), .y = static_cast<float>(north)},
      .heading = FromHalfDegrees(heading)};
}

}  // namespace

void Mission::Start(const MissionType& mission, const AiPolicies& policies, std::uint64_t seed) {
  random_ = Random{seed};
  policies_ = policies;
  const auto& player = kPlayerStarts[random_.Below(static_cast<std::uint32_t>(kPlayerStarts.size()))];
  player_ = PlaceVehicle(player.cell, player.east, player.north, player.heading);
  const auto target = kTargetStartCells[random_.Below(static_cast<std::uint32_t>(kTargetStartCells.size()))];
  target_ = PlaceVehicle(target, kTargetStartEast, kTargetStartNorth, kTargetStartHeading);
  traffic_count_ = 0;
  condition_ = CarCondition{};
  progress_ = MissionProgress{.mission = mission, .bounty = static_cast<std::int32_t>(mission.bounty)};
  station_.reset();
  tick_ = 0;
}

MissionEvents Mission::Step(const PlayerCommands& commands) {
  player_.controls = commands.controls;
  Drive(player_, kPlayerCar, road_, kTickSeconds);
  ++tick_;
  return MissionEvents{};
}

}  // namespace hp2
