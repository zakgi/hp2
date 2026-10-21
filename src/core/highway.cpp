#include "core/highway.hpp"

#include "core/engine_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace hp2 {

namespace {

// The sounds (UpdateSoundEffects, 0:1162; the original's volumes are 64, 63 and 32 of 64).
constexpr auto kFullVolume = 1.0F;
constexpr auto kLoudVolume = 63.0F / 64.0F;
constexpr auto kQuietVolume = 0.5F;
// The engine's sample plays at a period of 640 less the speed in units a frame, and was recorded
// for 540, the period at 100 units a frame; the speed counts up to 400.
constexpr auto kOriginalFramesPerSecond = 20.0F;
constexpr auto kEngineIdlePeriod = 640.0F;
constexpr auto kEngineSamplePeriod = 540.0F;
constexpr auto kEngineFastest = 400.0F;
// The tires squeal at 100 units a frame or more. The original asks for a slip of 50 half degrees,
// 0.44 rad, which a slide never reaches here: it settles at 0.3 rad (vehicle.cpp).
constexpr auto kSkidSpeed = 2000.0F;
constexpr auto kSkidSlip = 0.15F;

}  // namespace

// Lifecycle hooks use the instance-based ComponentLike protocol.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
void Highway::OnEnter(EngineState& state) {
  auto& highway = state.scene.emplace<HighwayState>();
  if (state.mission_under_way) {
    // Back from the station.
    state.mission.LeaveStation();
  } else {
    // The poster's bounty comes on top of the score: one counter, as the original's (1:236a).
    auto mission = kMissions[state.game.mission.value_or(0)];
    mission.bounty += state.game.score;
    const auto seed = (std::uint64_t{state.seeds.Next()} << 32U) | state.seeds.Next();
    state.mission.Start(highway.pathfinding, mission, state.policies, seed);
    state.driver_view.Reset(state.mission);
    state.mission_under_way = true;
  }
  state.keys.Clear();
  Draw(state);
}

void Highway::OnExit(EngineState& state) {
  StopSounds(state);
  state.scene.emplace<std::monostate>();
}

ComponentType Highway::Step(EngineState& state, float delta_seconds) {
  auto& highway = std::get<HighwayState>(state.scene);
  auto next = kType;
  const auto system_keys = ReadKeys(state);
  if (system_keys.abandon) {
    // As the original's Escape (0:bcb2): the bounty drops to 0, which ends the mission.
    state.mission.GetProgress().end_reason = EndReason::BountyGone;
    state.mission.GetProgress().bounty = 0;
    state.mission_under_way = false;
    next = ComponentType::MissionEnd;
  } else {
    ChangePhase(state, system_keys);
    if (highway.phase == HighwayPhase::Driving or highway.phase == HighwayPhase::Coasting) {
      next = RunTicks(state, delta_seconds);
      UpdateSounds(state);
    } else {
      // Silence while the game is paused, on the map or behind the spin-out picture.
      highway.pending_seconds = 0.0F;
      StopSounds(state);
    }
    if (next == kType) {
      Draw(state);
    }
  }
  return next;
}

// NOLINTEND(readability-convert-member-functions-to-static)

void Highway::ChangePhase(EngineState& state, const SystemKeys& system_keys) {
  auto& highway = std::get<HighwayState>(state.scene);
  if (highway.phase == HighwayPhase::SpinOut) {
    // The picture stays until a key.
    highway.phase = system_keys.any ? HighwayPhase::Driving : HighwayPhase::SpinOut;
  } else if (highway.phase != HighwayPhase::Coasting) {
    if (system_keys.map) {
      highway.phase = highway.phase == HighwayPhase::Map ? HighwayPhase::Driving : HighwayPhase::Map;
    } else if (system_keys.pause and highway.phase != HighwayPhase::Map) {
      highway.phase = highway.phase == HighwayPhase::Paused ? HighwayPhase::Driving : HighwayPhase::Paused;
    }
  }
}

ComponentType Highway::RunTicks(EngineState& state, float delta_seconds) {
  auto& highway = std::get<HighwayState>(state.scene);
  auto next = kType;
  highway.pending_seconds += delta_seconds;
  auto ticks = 0;
  while (highway.pending_seconds >= Mission::kTickSeconds and ticks < kMaxTicksPerFrame and next == kType and
         (highway.phase == HighwayPhase::Driving or highway.phase == HighwayPhase::Coasting)) {
    next = Handle(state, state.mission.Step(highway.pathfinding, TakeCommands(state)));
    highway.pending_seconds -= Mission::kTickSeconds;
    ++ticks;
  }
  // After a stall the rest is dropped: the game slows down instead of jumping.
  highway.pending_seconds = std::min(highway.pending_seconds, Mission::kTickSeconds);
  return next;
}

void Highway::Play(EngineState& state, EngineSound sound, std::size_t voice, float volume) {
  const auto& sample = state.assets.Sound(sound);
  auto& output = state.audio.GetVoice(voice);
  output.SetVolume(volume);
  output.SetStep(static_cast<float>(sample.rate_hz) / static_cast<float>(AudioEngine::kSampleRate));
  output.Start(sample.samples, sample.loop_start, std::size_t{sample.loop_start} + sample.loop_length);
}

void Highway::UpdateSounds(EngineState& state) {
  auto& highway = std::get<HighwayState>(state.scene);
  const auto& player = state.mission.GetPlayer();
  const auto speed = std::abs(player.speed);
  // The engine's note rises with the speed: the sample plays at its own rate at 100 units a frame.
  auto& engine = state.audio.GetVoice(kEngineVoice);
  if (not engine.Active()) {
    Play(state, EngineSound::Engine, kEngineVoice, kFullVolume);
  }
  const auto period = kEngineIdlePeriod - std::min(speed / kOriginalFramesPerSecond, kEngineFastest);
  engine.SetStep(static_cast<float>(state.assets.Sound(EngineSound::Engine).rate_hz) * kEngineSamplePeriod /
                 (period * static_cast<float>(AudioEngine::kSampleRate)));
  // The siren wails for as long as it is on.
  auto& siren = state.audio.GetVoice(kSirenVoice);
  if (state.mission.GetProgress().siren != siren.Active()) {
    if (siren.Active()) {
      siren.Stop();
    } else {
      Play(state, EngineSound::Siren, kSirenVoice, kLoudVolume);
    }
  }
  // The tires squeal while the car slides at speed, and once, quietly, as it leaves the road.
  const auto off_road = state.mission.GetCondition().off_road;
  if (speed >= kSkidSpeed and not state.audio.GetVoice(kSkidVoice).Active()) {
    if (std::abs(player.slip) >= kSkidSlip) {
      Play(state, EngineSound::Skid, kSkidVoice, kLoudVolume);
    } else if (off_road and not highway.was_off_road) {
      Play(state, EngineSound::Skid, kSkidVoice, kQuietVolume);
    }
  }
  highway.was_off_road = off_road;
}

void Highway::StopSounds(EngineState& state) {
  for (const auto voice : {kEngineVoice, kSirenVoice, kEffectVoice, kSkidVoice}) {
    state.audio.GetVoice(voice).Stop();
  }
}

ComponentType Highway::Handle(EngineState& state, MissionEvents events) {
  auto& highway = std::get<HighwayState>(state.scene);
  auto next = kType;
  const auto happened = [events](MissionEvent event) {
    return events.test(std::to_underlying(event));
  };
  // A crash and a shot share a voice, and the crash comes first: a shot neither replaces it nor
  // cuts it short (the original's priorities, 5 for the crash and 3 for the shot).
  highway.crash_sounding = highway.crash_sounding and state.audio.GetVoice(kEffectVoice).Active();
  if (happened(MissionEvent::Crash)) {
    Play(state, EngineSound::Crash, kEffectVoice, kLoudVolume);
    highway.crash_sounding = true;
  } else if ((happened(MissionEvent::Shot) or happened(MissionEvent::WindshieldHit)) and not highway.crash_sounding) {
    Play(state, EngineSound::Shot, kEffectVoice, kLoudVolume);
  }
  if (happened(MissionEvent::MissionOver)) {
    highway.phase = HighwayPhase::Coasting;
  }
  const auto station = state.mission.GetStation();
  if (happened(MissionEvent::StationStop) and station) {
    next = ComponentType::Station;
  } else if (happened(MissionEvent::SpinOut)) {
    highway.phase = HighwayPhase::SpinOut;
    highway.held = {};
  } else if (highway.phase == HighwayPhase::Coasting and state.mission.GetPlayer().speed == 0.0F) {
    // The cars have rolled to a stop: the ending, with what is left of the bounty as the score.
    state.game.score = static_cast<std::uint32_t>(std::max(state.mission.GetProgress().bounty, std::int32_t{0}));
    state.mission_under_way = false;
    next = ComponentType::MissionEnd;
  }
  return next;
}

Highway::SystemKeys Highway::ReadKeys(EngineState& state) {
  auto& highway = std::get<HighwayState>(state.scene);
  auto system_keys = SystemKeys{};
  while (const auto event = state.keys.Next()) {
    const auto pressed = event->action == KeyAction::Press;
    system_keys.any = system_keys.any or pressed;
    switch (event->key) {
      case Key::S:
        highway.siren_pressed = highway.siren_pressed or pressed;
        break;
      case Key::T:
        highway.aim_pressed = highway.aim_pressed or pressed;
        break;
      case Key::Left:
        highway.held.left = pressed;
        break;
      case Key::Right:
        highway.held.right = pressed;
        break;
      case Key::Up:
        highway.held.accelerate = pressed;
        break;
      case Key::Down:
        highway.held.brake = pressed;
        break;
      case Key::Space:
        highway.held.fire = pressed;
        break;
      case Key::P:
        system_keys.pause = system_keys.pause or pressed;
        break;
      case Key::M:
        system_keys.map = system_keys.map or pressed;
        break;
      case Key::Escape:
        system_keys.abandon = system_keys.abandon or pressed;
        break;
      default:
        break;
    }
  }
  return system_keys;
}

PlayerCommands Highway::TakeCommands(EngineState& state) {
  auto& highway = std::get<HighwayState>(state.scene);
  // The wheel follows the held keys at kKeySteerRate, back to the middle when they are released.
  const auto target = static_cast<float>(highway.held.left) - static_cast<float>(highway.held.right);
  const auto step = kKeySteerRate * Mission::kTickSeconds;
  highway.key_steer = target > highway.key_steer ? std::min(highway.key_steer + step, target)
                                                 : std::max(highway.key_steer - step, target);
  const auto throttle = static_cast<float>(highway.held.accelerate) - static_cast<float>(highway.held.brake);
  auto commands = PlayerCommands{.controls = {.steer = highway.key_steer, .throttle = throttle},
                                 .firing = highway.held.fire,
                                 .toggle_siren = highway.siren_pressed,
                                 .toggle_aim = highway.aim_pressed};
  highway.siren_pressed = false;
  highway.aim_pressed = false;
  return commands;
}

void Highway::Draw(EngineState& state) {
  auto& highway = std::get<HighwayState>(state.scene);
  if (highway.phase == HighwayPhase::Map) {
    state.map_view.Draw(state.mission.GetPlayer().position, state.mission.GetTarget().position);
  } else if (highway.phase == HighwayPhase::SpinOut) {
    state.screen.DisableSplit();
    state.screen.Palette(Viewport::Upper).Reset();
    state.screen.Palette(Viewport::Upper).Overlay(state.assets.Palette(EnginePalette::SpinOut), 0);
    state.screen.Blit(state.assets.Picture(EnginePicture::SpinOut), Point{});
  } else {
    state.driver_view.Draw(state.mission);
  }
}

}  // namespace hp2
