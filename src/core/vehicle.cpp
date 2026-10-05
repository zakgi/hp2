#include "core/vehicle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hp2 {

namespace {

// Below this speed the grip no longer limits the yaw rate (the limit divides by the speed).
constexpr auto kMinGripSpeed = 1.0F;  // units per second

// Skidding (the original's: 0:3d0c). When the wheel asks for more than the grip gives, the body
// goes on turning, up to kSlideRate faster than the way the car goes, and the angle between them
// is the slip, kMaxSlip at most. The tires then bite again: a second takes kSlipRecovery of the
// slip away, kSlipTurn of that turning the car after its body, and each radian of slip scrubs
// kSlipScrub off the speed. The original loses a tenth of the slip a frame, half of it into the
// heading, and a twentieth of the slip's half degrees in speed.
constexpr auto kSlideRate = 0.6F;     // radians per second
constexpr auto kMaxSlip = 0.8F;       // radians
constexpr auto kSlipRecovery = 2.0F;  // per second
constexpr auto kSlipTurn = 0.5F;
constexpr auto kSlipScrub = 2300.0F;  // units per second squared, per radian

// The body rides its springs as the car moves, one wave every 40 units of the original's count,
// which advances by a fiftieth of the speed a frame and by one at least (0:4068, the wave at
// 0:4930): 6 units either way on the road, four times that off it.
constexpr auto kBounce = 6.0F;
constexpr auto kRoughBounce = 24.0F;
constexpr auto kMinBounceRate = 0.5F;                 // waves per second
constexpr auto kBounceRatePerSpeed = 0.5F / 1000.0F;  // waves per second, per unit per second

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

// Impacts (UpdateImpacts, 0:4068), the original's amounts a frame as rates.
//
// A bump over stones: the body rides one of four profiles, a step a frame, rougher for every 100
// units a frame of speed at the start (0:4980), and the car loses a twentieth of its speed a frame.
constexpr auto kBumpLevelSpeed = 2000.0F;
constexpr auto kLastBumpLevel = 3.0F;
constexpr auto kBumpDecay = 1.03F;  // of the speed, per second
constexpr auto kGentleBump = std::to_array<float>({0, 20, 0, -5});
constexpr auto kFairBump = std::to_array<float>({0, 40, 40, 20, 0, -10, 4});
constexpr auto kHardBump = std::to_array<float>({0, 30, 60, 60, 30, 0, -15, -6, 6, -6});
constexpr auto kRoughBump = std::to_array<float>({0, 40, 70, 80, 80, 75, 60, 40, 0, -20, -8, 8, -8});
constexpr auto kBumps = std::to_array<std::span<const float>>({kGentleBump, kFairBump, kHardBump, kRoughBump});

// A spin after hitting something solid: the car is thrown back 30 units and two frames' travel and
// turned 90 half degrees away. Then each frame the body turns by an eighth of the speed in half
// degrees and the car's way by a twelfth, the car loses an eighth of its speed and the body shakes
// by a tenth of it, a frame up and a frame down, until the car is slower than 20 units a frame.
constexpr auto kPushBack = 30.0F;
constexpr auto kPushBackSeconds = 2.0F / kOriginalFramesPerSecond;
constexpr auto kSpinTurn = kFullTurn / 8.0F;
constexpr auto kSpinBodyRate = kFullTurn / (720.0F * 8.0F);  // radians per unit travelled
constexpr auto kSpinWayRate = kFullTurn / (720.0F * 12.0F);
constexpr auto kSpinDecay = 2.67F;  // of the speed, per second
constexpr auto kSpinShake = 1.0F / (10.0F * kOriginalFramesPerSecond);
constexpr auto kSpinEndSpeed = 400.0F;

// The bump of `vehicle` carried on by `seconds`.
void UpdateBump(Vehicle& vehicle, float seconds) {
  auto& impact = vehicle.impact;
  const auto profile = kBumps[static_cast<std::size_t>(std::lround(impact.strength * kLastBumpLevel))];
  const auto step = impact.seconds * kOriginalFramesPerSecond;
  const auto index = static_cast<std::size_t>(step);
  if (index + 1 < profile.size()) {
    vehicle.body_lift = std::lerp(profile[index], profile[index + 1], step - static_cast<float>(index));
    vehicle.speed -= vehicle.speed * std::min(1.0F, kBumpDecay * seconds);
    impact.seconds += seconds;
  } else {
    vehicle.body_lift = 0.0F;
    impact = Impact{};
  }
}

// The spin of `vehicle` carried on by `seconds`.
void UpdateSpin(Vehicle& vehicle, float seconds) {
  auto& impact = vehicle.impact;
  const auto speed = std::abs(vehicle.speed);
  if (speed >= kSpinEndSpeed) {
    vehicle.slip += impact.spin * speed * kSpinBodyRate * seconds;
    vehicle.heading = WrapAngle(vehicle.heading + (impact.spin * speed * kSpinWayRate * seconds));
    vehicle.speed -= vehicle.speed * std::min(1.0F, kSpinDecay * seconds);
    const auto rising = static_cast<int>(impact.seconds * kOriginalFramesPerSecond) % 2 == 0;
    vehicle.body_lift = speed * kSpinShake * (rising ? 1.0F : -1.0F);
    impact.seconds += seconds;
  } else {
    // Slow enough: the car stands, pointing the way its body does.
    vehicle.speed = 0.0F;
    vehicle.heading = vehicle.GetBodyHeading();
    vehicle.slip = 0.0F;
    vehicle.body_lift = 0.0F;
    impact = Impact{};
  }
}

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

BodyPoint GetBodyPoint(const Vehicle& vehicle, WorldPoint point) {
  const auto forward = GetDirection(vehicle.GetBodyHeading());
  const auto apart = point - vehicle.position;
  return BodyPoint{.ahead = Dot(apart, forward), .right = -Cross(forward, apart)};
}

void StartBump(Vehicle& vehicle) {
  const auto level = std::min(std::floor(std::abs(vehicle.speed) / kBumpLevelSpeed), kLastBumpLevel);
  vehicle.impact =
      Impact{.kind = ImpactKind::kBump, .strength = level / kLastBumpLevel, .held_steer = vehicle.controls.steer};
}

void StartSpin(Vehicle& vehicle, bool on_right) {
  // Away from what it hit: to the left of its way when that stood on its right.
  const auto away = on_right ? 1.0F : -1.0F;
  const auto thrown = (vehicle.speed * kPushBackSeconds) + std::copysign(kPushBack, vehicle.speed);
  vehicle.position = vehicle.position - (GetDirection(vehicle.heading) * thrown);
  vehicle.heading = WrapAngle(vehicle.heading + (away * kSpinTurn));
  vehicle.slip -= away * kSpinTurn;
  vehicle.impact = Impact{.kind = ImpactKind::kSpin, .spin = -away};
}

void Drive(Vehicle& vehicle, const VehicleTuning& tuning, const Road& road, float seconds) {
  // An impact takes the throttle away, and a bump holds the wheel where it was.
  const auto impact = vehicle.impact.kind;
  const auto throttle = impact == ImpactKind::kNone ? vehicle.controls.throttle : 0.0F;
  const auto steer = impact == ImpactKind::kBump ? vehicle.impact.held_steer : vehicle.controls.steer;
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
  const auto on_road = road.IsOnRoad(vehicle.position);
  if (not on_road) {
    speed -= speed * std::min(1.0F, tuning.off_road_drag * seconds);
  }
  // The yaw rate the wheel asks for, held to what the tires can take sideways at this speed.
  const auto wanted_yaw = speed * steer / tuning.turning_radius;
  const auto max_yaw = tuning.grip / std::max(std::abs(speed), kMinGripSpeed);
  const auto yaw = std::clamp(wanted_yaw, -max_yaw, max_yaw);
  auto turned = yaw * seconds;
  if (impact != ImpactKind::kSpin) {
    // The slip there is dies away, turning the car after its body and scrubbing its speed.
    const auto recovered = vehicle.slip * std::min(1.0F, kSlipRecovery * seconds);
    speed -= std::copysign(std::min(std::abs(speed), kSlipScrub * std::abs(vehicle.slip) * seconds), speed);
    // Past the grip the body turns on ahead of the way the car goes.
    const auto slide = std::clamp(wanted_yaw - yaw, -kSlideRate, kSlideRate);
    vehicle.slip = std::clamp(vehicle.slip - recovered + (slide * seconds), -kMaxSlip, kMaxSlip);
    turned += recovered * kSlipTurn;
  }
  vehicle.heading = WrapAngle(vehicle.heading + turned);
  vehicle.position = vehicle.position + (GetDirection(vehicle.heading) * (speed * seconds));
  vehicle.speed = speed;
  if (impact == ImpactKind::kBump) {
    UpdateBump(vehicle, seconds);
  } else if (impact == ImpactKind::kSpin) {
    UpdateSpin(vehicle, seconds);
  } else if (speed != 0.0F) {
    const auto rate = std::max(kMinBounceRate, std::abs(speed) * kBounceRatePerSpeed);
    vehicle.bounce_phase += rate * seconds;
    vehicle.bounce_phase -= std::floor(vehicle.bounce_phase);
    vehicle.body_lift = (on_road ? kBounce : kRoughBounce) * std::sin(kFullTurn * vehicle.bounce_phase);
  }
}

}  // namespace hp2
