#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hp2 {

enum class MissionType : std::uint8_t {
  kPullOver,   // stop the criminal with the siren on
  kRoadblock,  // let the criminal ram you while you stand still
  kShoot,      // five hits with the gun
};

struct Mission {
  MissionType type;
  std::uint16_t target_max_speed;
  std::uint32_t bounty;
};

// The six wanted posters, in poster order (missionTable, 1:23ae; docs/game.md, "Missions").
inline constexpr auto kMissions = std::to_array<Mission>({
    {.type = MissionType::kPullOver, .target_max_speed = 200, .bounty = 2000},
    {.type = MissionType::kPullOver, .target_max_speed = 250, .bounty = 2000},
    {.type = MissionType::kRoadblock, .target_max_speed = 300, .bounty = 5000},
    {.type = MissionType::kRoadblock, .target_max_speed = 350, .bounty = 5000},
    {.type = MissionType::kShoot, .target_max_speed = 350, .bounty = 10000},
    {.type = MissionType::kShoot, .target_max_speed = 400, .bounty = 10000},
});
inline constexpr auto kMissionCount = kMissions.size();

// Why a mission ended (CheckMissionEnd, 0:6008; docs/game.md, "Scenes and endings").
enum class EndReason : std::uint8_t {
  kOutOfFuel,
  kStationsRobbed,
  kOverheated,
  kWrecked,
  kTiresGone,
  kShot,
  kArrest,
  kBountyGone,
};

// What the screens between missions share: the career, the mission under way and the state of
// the player's car the station and the endings read.
struct GameState {
  static constexpr std::uint8_t kFullTires = 2;

  // The bounty earned, which doubles as the score.
  std::uint32_t score{};
  std::array<bool, kMissionCount> missions_available{true, true, true, true, true, true};
  // Index into kMissions of the mission chosen in the office.
  std::optional<std::size_t> mission;
  std::optional<EndReason> end_reason;
  // 0 (empty) to 1 (full).
  float fuel{1.0F};
  std::uint8_t tires{kFullTires};
  // Whether the station the player stopped at has been robbed.
  bool station_robbed{};

  [[nodiscard]] std::size_t MissionsLeft() const {
    return static_cast<std::size_t>(std::ranges::count(missions_available, true));
  }
  // A new career: no score, every poster back.
  void ResetCareer() {
    score = 0;
    missions_available.fill(true);
    mission.reset();
    end_reason.reset();
  }
};

}  // namespace hp2
