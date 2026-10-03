#pragma once

#include <cstdint>

#include "core/game_state.hpp"
#include "core/road.hpp"
#include "core/world.hpp"

namespace hp2 {

// What a driver asks of a car, from the keys or from the AI; the same drive model obeys both.
struct Controls {
  // The wheel's position: -1 full right .. 1 full left. A steering wheel sets it directly, held keys
  // ramp it (Highway), the AI smooths its own.
  float steer{};
  // -1 brake, and reverse once stopped .. 1 accelerate.
  float throttle{};
};

// What a collision does to a car while it lasts. The original runs five channels and a crash
// state side by side (UpdateImpacts, 0:4068); here a car has one impact at a time.
enum class ImpactKind : std::uint8_t {
  kNone,
  kBump,   // stones: the body rides a height profile, the steering is held
  kSpin,   // cactus, sign or car: turned away from the contact, spinning until slow
  kCrash,  // too long off the road at speed: shaking to a stop, a tire lost
};

struct Impact {
  ImpactKind kind{ImpactKind::kNone};
  float seconds{};     // since it started
  float spin_rate{};   // radians per second, counter-clockwise positive
  float strength{};    // 0..1, from the speed at the contact
  float held_steer{};  // kBump: the wheel's position when it started
};

// One car: the player's, the criminal's or traffic. Speeds are units per second, angles radians.
struct Vehicle {
  WorldPoint position;
  // Where the car travels.
  float heading{};
  // The body's angle to the travel direction while sliding or spinning: the car is drawn, and the
  // player's view looks, along heading + slip.
  float slip{};
  // Along heading; negative in reverse.
  float speed{};
  // The body above its rest height, units: road bounce, bumps, shaking. The player's eye rides it.
  float body_lift{};
  // Where the body is in its road bounce, 0..1 of a cycle (the original's 40-step wave, 0:4930).
  float bounce_phase{};
  Controls controls;
  Impact impact;

  [[nodiscard]] float GetBodyHeading() const { return WrapAngle(heading + slip); }
};

// How a kind of car drives, tuned for the port; the original's per-frame values are in
// docs/vehicles.md, section 4.
struct VehicleTuning {
  float top_speed{};          // units per second
  float top_reverse_speed{};  // units per second
  float acceleration{};       // units per second squared
  float braking{};            // units per second squared
  float drag{};               // fraction of the speed lost per second without throttle
  float max_steering{};       // the front wheels' angle at full lock, radians
  float wheelbase{};          // units
  float grip{};               // units per second squared sideways before the car slides
};

// Moves `vehicle` by `seconds` under its controls: wheel, speed, yaw, slip, position, body lift
// (DriveVehicle 0:3a2e and MoveVehicle 0:3e7e, reworked; docs/highway.md, "Handling").
void Drive(Vehicle& vehicle, const VehicleTuning& tuning, const Road& road, float seconds);

// What only the player's car tracks: the dashboard's gauges and what ends a mission.
struct CarCondition {
  float fuel{1.0F};     // 0 empty .. 1 full
  float temperature{};  // 0 cold .. 1 overheated
  float damage{};       // 0 .. 1 wrecked
  std::uint8_t tires{GameState::kFullTires};
  float off_road_seconds{};  // at speed, since the car last held the road
  std::uint8_t gear{1};      // automatic, 1..5 (0:3bec)
  float rpm{};

  // Fuel, temperature, gear and rpm after `seconds` of driving as `vehicle`.
  void Update(const Vehicle& vehicle, float seconds);
};

}  // namespace hp2
