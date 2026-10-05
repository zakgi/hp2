#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "core/ai.hpp"
#include "core/game_state.hpp"
#include "core/random.hpp"
#include "core/road.hpp"
#include "core/vehicle.hpp"

namespace hp2 {

// What the player does in one tick: the car's controls from the held keys, and the actions single
// presses trigger.
struct PlayerCommands {
  Controls controls;
  bool firing{};        // fire held while aiming
  bool toggle_siren{};  // S
  bool toggle_aim{};    // T
};

// What happened during a tick, for the sounds and the scenes.
enum class MissionEvent : std::uint8_t {
  kCrash,          // the player's car hit something
  kShot,           // the player fired
  kTargetHit,      // the shot hit the criminal's car
  kTrafficHit,     // the shot hit a traffic car
  kWindshieldHit,  // the criminal's bullet went through the windshield
  kStationStop,    // the player's car stopped at a station's pumps
  kSpinOut,        // a tire lost leaving the road (PAGE_F1); the mission goes on
  kMissionOver,    // the reason is in GetProgress().end_reason
  kCount,
};
using MissionEvents = std::bitset<std::to_underlying(MissionEvent::kCount)>;

// A direction from the player's eye relative to the body, for the sight and the bullet holes: the
// mission decides what they hit, the views place them.
struct ViewDirection {
  float bearing{};    // radians, left positive
  float elevation{};  // radians, up positive
};

// The state of the mission's rules (docs/game.md, "Missions").
struct MissionProgress {
  static constexpr std::size_t kMaxWindshieldHoles = 10;

  MissionType mission{};
  // Dollars: drains with time and penalties, and doubles as the score.
  std::int32_t bounty{};
  std::bitset<kStationCount> robbed;
  bool siren{};
  bool aiming{};
  ViewDirection sight;
  // The last shot hit a car: the views show it at the sight until the next shot.
  bool shot_hit{};
  // What is left of the red flash a contact with another car sets off (0:4906: two frames).
  float flash_seconds{};
  std::uint8_t target_hits{};
  std::array<ViewDirection, kMaxWindshieldHoles> holes{};
  std::uint8_t hole_count{};
  std::optional<EndReason> end_reason;
};

// Whether the criminal's car is where a pull-over arrest takes it (CheckArrest, 0:4f14): in the
// player's cell, in the driver's view less than 1000 units ahead, and heading within a quarter turn
// of the player's way.
[[nodiscard]] bool IsInArrestReach(const Vehicle& player, const Vehicle& target);

// One mission, advanced a fixed tick at a time: the player's car, the criminal's, the traffic, their
// drivers and the rules. It knows nothing of the screen, the keys or the sound: the Highway
// component feeds it commands and turns its state and events into pictures and noise, so a mission
// runs headless and replays exactly from its seed and commands. A value: copying it snapshots the
// mission.
class Mission {
 public:
  static constexpr float kTickSeconds = 1.0F / 60.0F;
  static constexpr std::size_t kMaxTraffic = 4;

  explicit Mission(Road road) : road_(road) {}

  // A new mission run with `policies`: the player at one of three starts (0:b76e), the criminal at
  // one of 16 crossroads (1:236e), no traffic yet.
  void Start(const MissionType& mission, const AiPolicies& policies, std::uint64_t seed);
  // Back on the road after the station, with the fuel and tires the station left, at the driveway's
  // exit (0:328e).
  void LeaveStation(float fuel, std::uint8_t tires);
  // One tick.
  MissionEvents Step(const PlayerCommands& commands);

  [[nodiscard]] const Road& GetRoad() const { return road_; }
  [[nodiscard]] const Vehicle& GetPlayer() const { return player_; }
  // The criminal's car.
  [[nodiscard]] const Vehicle& GetTarget() const { return target_; }
  [[nodiscard]] std::span<const Vehicle> GetTraffic() const {
    return std::span<const Vehicle>{traffic_}.first(traffic_count_);
  }
  [[nodiscard]] const CarCondition& GetCondition() const { return condition_; }
  [[nodiscard]] const MissionProgress& GetProgress() const { return progress_; }
  // The index into Road::GetStations() of the station the player stopped at, after kStationStop.
  [[nodiscard]] std::optional<std::size_t> GetStation() const { return station_; }
  [[nodiscard]] std::uint32_t GetTick() const { return tick_; }

 private:
  void StepPlayer(const PlayerCommands& commands, MissionEvents& events);
  // The player's car shaking to a stop on a lost tire.
  void StepCrash(MissionEvents& events);
  // Counts the time off the road at speed; too much of it costs a tire.
  void TrackOffRoad(MissionEvents& events);
  void StepTarget(MissionEvents& events);
  void StepTraffic();
  // A new traffic car where the policy puts one, if it does.
  void SpawnTraffic();
  // Scenery and car-to-car contacts of every car, into impacts and damage.
  void ResolveContacts(MissionEvents& events);
  // What two cars that have just come into contact do to each other, and to the bounty when the
  // first is the player's; `second_is_target` when the other is the criminal's.
  void HitCars(Vehicle& first, Vehicle& second, bool first_is_player, bool second_is_target, MissionEvents& events);
  // The cars in one list: the player's, the criminal's, then the traffic's.
  [[nodiscard]] Vehicle& GetCar(std::size_t index);
  // The player's shots and the criminal's.
  void ResolveShots(const PlayerCommands& commands, MissionEvents& events);
  // One shot of the player's gun along the sight: the nearest car it meets is hit.
  void FirePlayerShot(MissionEvents& events);
  // One shot of the criminal's along its way: a hole in the player's windshield, or worse.
  void FireTargetShot(MissionEvents& events);
  // Whether the mission's kind of arrest has been made.
  [[nodiscard]] bool IsArrested() const;
  // Arrest, bounty, and the end of the mission.
  void ApplyRules(MissionEvents& events);
  // What the policies look at, the two distance fields brought up to date, with `robbed` as the
  // stations they may not choose.
  [[nodiscard]] PolicyContext GetPolicyContext(const std::bitset<kStationCount>& robbed);
  // Routes the criminal to the station its policy picks outside `skipped`; false when there is none.
  bool SendTarget(const std::bitset<kStationCount>& skipped);
  // The criminal stopped at the pumps: robs the station if its policy says so, then leaves for
  // another.
  void RobStation();

  Road road_;
  Random random_;
  AiPolicies policies_;
  // For the policies: rebuilt when the criminal or the player changes cell.
  RouteField target_distances_;
  RouteField player_distances_;
  Vehicle player_;
  Vehicle target_;
  AiDriver target_driver_;
  std::array<Vehicle, kMaxTraffic> traffic_{};
  std::array<AiDriver, kMaxTraffic> traffic_drivers_{};
  std::size_t traffic_count_{};
  CarCondition condition_;
  MissionProgress progress_;
  std::optional<std::size_t> station_;
  // The criminal's car ran into the player's while it stood still: the roadblock arrest.
  bool rammed_standing_{};
  // A bullet of the criminal's found the driver.
  bool shot_dead_{};
  std::uint32_t tick_{};
};

}  // namespace hp2
