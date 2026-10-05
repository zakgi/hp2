#include "core/vehicle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hp2 {

namespace {

// Below this speed the grip no longer limits the yaw rate (the limit divides by the speed).
constexpr auto kMinGripSpeed = 1.0F;  // units per second

// The car's condition follows the original's rules (DriveVehicle, 0:3a2e; docs/vehicles.md,
// section 7.2), which count speed in units a frame and change once a frame: the port takes 20
// frames a second (docs/highway.md, "Time").
constexpr auto kOriginalFramesPerSecond = 20.0F;

// The automatic transmission (0:3bec): a gear for every 80 units of speed, and in each the engine
// turns `rpm` at the gear's lowest speed and `rise` more for each unit above it.
struct Gear {
  float rpm;
  float rise;
};
constexpr auto kGearSpeed = 80.0F;
constexpr auto kGears = std::to_array<Gear>({
    {.rpm = 0.0F, .rise = 24.0F / 8.0F},
    {.rpm = 130.0F, .rise = 15.0F / 8.0F},
    {.rpm = 170.0F, .rise = 13.0F / 8.0F},
    {.rpm = 210.0F, .rise = 11.0F / 8.0F},
    {.rpm = 250.0F, .rise = 15.0F / 8.0F},
});

// The tank and the temperature are words in the original, 65536 for full and for overheated. A
// frame burns the square of an eighth of the speed, over 64; it heats the engine by 100 when it
// turns at kHotRpm or more and cools it by as much when it does not.
constexpr auto kWordRange = 65536.0F;
constexpr auto kFuelSpeedDivisor = 8.0F;
constexpr auto kFuelDivisor = 64.0F;
constexpr auto kHotRpm = 300.0F;
constexpr auto kTemperatureStep = 100.0F;

}  // namespace

void CarCondition::Update(const Vehicle& vehicle, float seconds) {
  // In the original's units; backward counts as forward.
  const auto speed = std::abs(vehicle.speed) / kOriginalFramesPerSecond;
  const auto frames = seconds * kOriginalFramesPerSecond;
  const auto number = std::clamp(std::ceil(speed / kGearSpeed), 1.0F, static_cast<float>(kGears.size()));
  gear = static_cast<std::uint8_t>(number);
  const auto& ratio = kGears[gear - 1U];
  rpm = ratio.rpm + (ratio.rise * (speed - ((number - 1.0F) * kGearSpeed)));
  const auto burned = (speed / kFuelSpeedDivisor) * (speed / kFuelSpeedDivisor) / kFuelDivisor;
  fuel = std::max(0.0F, fuel - (burned * frames / kWordRange));
  const auto heating = rpm >= kHotRpm ? kTemperatureStep : -kTemperatureStep;
  temperature = std::clamp(temperature + (heating * frames / kWordRange), 0.0F, 1.0F);
}

void Drive(Vehicle& vehicle, const VehicleTuning& tuning, const Road& road, float seconds) {
  const auto throttle = vehicle.controls.throttle;
  auto speed = vehicle.speed;
  if (throttle > 0.0F) {
    speed = speed < 0.0F ? std::min(0.0F, speed + (tuning.braking * throttle * seconds))
                         : std::min(tuning.top_speed, speed + (tuning.acceleration * throttle * seconds));
  } else if (throttle < 0.0F) {
    speed = speed > 0.0F ? std::max(0.0F, speed + (tuning.braking * throttle * seconds))
                         : std::max(-tuning.top_reverse_speed, speed + (tuning.acceleration * throttle * seconds));
  } else {
    speed -= speed * std::min(1.0F, tuning.drag * seconds);
  }
  if (not road.IsOnRoad(vehicle.position)) {
    speed -= speed * std::min(1.0F, tuning.off_road_drag * seconds);
  }
  // The yaw rate the wheel asks for, held to what the tires can take sideways at this speed.
  const auto wanted_yaw = speed * vehicle.controls.steer / tuning.turning_radius;
  const auto max_yaw = tuning.grip / std::max(std::abs(speed), kMinGripSpeed);
  vehicle.heading = WrapAngle(vehicle.heading + (std::clamp(wanted_yaw, -max_yaw, max_yaw) * seconds));
  vehicle.position = vehicle.position + (GetDirection(vehicle.heading) * (speed * seconds));
  vehicle.speed = speed;
}

}  // namespace hp2
