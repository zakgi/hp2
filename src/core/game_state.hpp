#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hp2 {

// How the criminal is arrested in a mission.
enum class ArrestMethod : std::uint8_t {
  PullOver,   // stop the criminal with the siren on
  Roadblock,  // let the criminal ram you while you stand still
  Shoot,      // five hits with the gun
};

struct MissionType {
  ArrestMethod arrest;
  std::uint16_t target_max_speed;
  std::uint32_t bounty;
};

// The six wanted posters, in poster order (missionTable, 1:23ae; docs/game.md, "Missions").
inline constexpr auto kMissions = std::to_array<MissionType>({
    {.arrest = ArrestMethod::PullOver, .target_max_speed = 200, .bounty = 2000},
    {.arrest = ArrestMethod::PullOver, .target_max_speed = 250, .bounty = 2000},
    {.arrest = ArrestMethod::Roadblock, .target_max_speed = 300, .bounty = 5000},
    {.arrest = ArrestMethod::Roadblock, .target_max_speed = 350, .bounty = 5000},
    {.arrest = ArrestMethod::Shoot, .target_max_speed = 350, .bounty = 10000},
    {.arrest = ArrestMethod::Shoot, .target_max_speed = 400, .bounty = 10000},
});
inline constexpr auto kMissionCount = kMissions.size();

// Why a mission ended (CheckMissionEnd, 0:6008; docs/game.md, "Scenes and endings").
enum class EndReason : std::uint8_t {
  OutOfFuel,
  StationsRobbed,
  Overheated,
  Wrecked,
  TiresGone,
  Shot,
  Arrest,
  BountyGone,
};

// Career data shared by the controllers. Active car condition and ending live in Mission.
struct GameState {
  static constexpr std::uint8_t kFullTires = 2;

  // The bounty earned, which doubles as the score.
  std::uint32_t score{};
  std::array<bool, kMissionCount> missions_available{true, true, true, true, true, true};
  // Index into kMissions of the mission chosen in the office.
  std::optional<std::size_t> mission;

  [[nodiscard]] std::size_t MissionsLeft() const {
    return static_cast<std::size_t>(std::ranges::count(missions_available, true));
  }
  // A new career: no score, every poster back.
  void ResetCareer() {
    score = 0;
    missions_available.fill(true);
    mission.reset();
  }
};

}  // namespace hp2
