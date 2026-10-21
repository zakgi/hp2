#pragma once

#include <cstdint>
#include <variant>

#include "core/audio_engine.hpp"
#include "core/driver_view.hpp"
#include "core/engine_assets.hpp"
#include "core/game_state.hpp"
#include "core/highway.hpp"
#include "core/key_events.hpp"
#include "core/map_view.hpp"
#include "core/mission.hpp"
#include "core/mission_end.hpp"
#include "core/office.hpp"
#include "core/random.hpp"
#include "core/screen.hpp"
#include "core/station.hpp"
#include "core/title.hpp"

namespace hp2 {

// Scene data has one active alternative; the mission survives a station visit outside it.
using SceneState = std::variant<std::monostate, TitleState, OfficeState, HighwayState, StationState, MissionEndState>;

// Persistent game and scene storage. The platform owns the assets, screen, input queue and audio
// backend; they outlive this root. Construct it once after assets are bound. Its address stays
// fixed because the views refer to the mission's road and active actions refer to palettes.
struct EngineState {
  EngineState(const EngineAssets& assets, Screen& screen, KeyEvents& keys, AudioEngine& audio, std::uint64_t seed)
      : assets(assets),
        screen(screen),
        keys(keys),
        audio(audio),
        mission(Road{assets.road_map, assets.road_shapes, assets.scenery}),
        driver_view(assets, screen, mission.GetRoad()),
        map_view(screen, mission.GetRoad()),
        seeds(seed) {}

  EngineState(const EngineState&) = delete;
  EngineState& operator=(const EngineState&) = delete;
  EngineState(EngineState&&) = delete;
  EngineState& operator=(EngineState&&) = delete;

  const EngineAssets& assets;
  Screen& screen;
  KeyEvents& keys;
  AudioEngine& audio;
  GameState game;
  Mission mission;
  DriverView driver_view;
  MapView map_view;
  // Configuration, the career seed stream and mission lifetime survive scene switches.
  AiPolicies policies{};
  Random seeds;
  bool mission_under_way{};
  SceneState scene;
};

}  // namespace hp2
