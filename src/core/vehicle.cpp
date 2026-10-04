#include "core/vehicle.hpp"

#include <algorithm>
#include <cmath>

namespace hp2 {

namespace {

// Below this speed the grip no longer limits the yaw rate (the limit divides by the speed).
constexpr auto kMinGripSpeed = 1.0F;  // units per second

}  // namespace

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
