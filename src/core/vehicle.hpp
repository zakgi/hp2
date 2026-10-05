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
  float spin{};        // kSpin: 1 counter-clockwise, -1 clockwise; how fast follows the speed
  float strength{};    // kBump: 0..1 for the gentlest to the roughest of the four profiles
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
  // The car's paint: 0 the criminal's red, 1 to 7 the traffic's schemes (0:e5d0).
  std::uint8_t color_scheme{};
  // Against another car at the last tick: a contact counts once, when it starts.
  bool touching{};

  [[nodiscard]] float GetBodyHeading() const { return WrapAngle(heading + slip); }
};

// A car's hit box: half its width and half its length on the ground, units, about the car's middle
// and turned with its body. The original tests a box that follows the view, 120 units left to 220
// right and 200 behind to 400 and a frame's travel ahead (0:25bc).
inline constexpr auto kHitBoxHalfWidth = 120.0F;
inline constexpr auto kHitBoxHalfLength = 250.0F;

// A point as a car's body sees it: how far ahead of its middle and how far to its right.
struct BodyPoint {
  float ahead{};
  float right{};
};
[[nodiscard]] BodyPoint GetBodyPoint(const Vehicle& vehicle, WorldPoint point);

// Starts the bump of a car over stones, rougher the faster it goes (UpdateImpacts 0:4068, channel
// A): Drive then holds the wheel and has the body ride the bump's profile.
void StartBump(Vehicle& vehicle);
// Starts the spin of a car that hit something solid on its right, or on its left (channels B, C
// and E): thrown back and turned an eighth of a turn away from it, the body still pointing as it
// did. Drive then spins the body round as the car slides to a stop.
void StartSpin(Vehicle& vehicle, bool on_right);

// How a kind of car drives, tuned for the port; the original's per-frame values are in
// docs/vehicles.md, section 4.
struct VehicleTuning {
  float top_speed{};          // units per second
  float top_reverse_speed{};  // units per second
  float acceleration{};       // units per second squared
  float braking{};            // units per second squared
  float drag{};               // fraction of the speed lost per second without throttle
  float off_road_drag{};      // fraction of the speed lost per second off the road
  float turning_radius{};     // units, at full lock
  float grip{};               // units per second squared sideways before the car slides
};

// Moves `vehicle` by `seconds` under its controls: wheel, speed, yaw, slip, position, body lift
// (DriveVehicle 0:3a2e and MoveVehicle 0:3e7e, reworked; docs/highway.md, "Handling"). An impact
// under way takes the throttle away, and a bump or a spin runs its course here.
void Drive(Vehicle& vehicle, const VehicleTuning& tuning, const Road& road, float seconds);

// What only the player's car tracks: the dashboard's gauges and what ends a mission.
struct CarCondition {
  float fuel{1.0F};     // 0 empty .. 1 full
  float temperature{};  // 0 cold .. 1 overheated
  float damage{};       // 0 .. 1 wrecked
  std::uint8_t tires{GameState::kFullTires};
  float off_road_seconds{};  // at speed; half of it is forgiven when the car is back on the road
  bool off_road{};           // at the last tick
  std::uint8_t gear{1};      // automatic, 1..5 (0:3bec)
  float rpm{};               // as the original counts it, 0 to about 400: what the tachometer shows

  // Fuel, temperature, gear and rpm after `seconds` of driving as `vehicle`.
  void Update(const Vehicle& vehicle, float seconds);
};

}  // namespace hp2
