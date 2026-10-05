#include "core/highway.hpp"

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

Highway::Highway(const EngineAssets& assets, Screen& screen, KeyEvents& keys, AudioEngine& audio, GameState& game,
                 std::uint64_t seed)
    : assets_(assets),
      screen_(screen),
      keys_(keys),
      audio_(audio),
      game_(game),
      mission_(Road{assets.road_map, assets.road_shapes, assets.scenery}),
      driver_view_(assets, screen, mission_.GetRoad()),
      map_view_(screen, mission_.GetRoad()),
      seeds_(seed) {}

void Highway::OnEnter() {
  if (mission_under_way_) {
    // Back from the station.
    mission_.LeaveStation(game_.fuel, game_.tires);
  } else {
    // The poster's bounty comes on top of the score: one counter, as the original's (1:236a).
    auto mission = kMissions[game_.mission.value_or(0)];
    mission.bounty += game_.score;
    const auto seed = (std::uint64_t{seeds_.Next()} << 32U) | seeds_.Next();
    mission_.Start(mission, policies_, seed);
    mission_under_way_ = true;
  }
  phase_ = Phase::kDriving;
  held_ = {};
  key_steer_ = 0.0F;
  siren_pressed_ = false;
  aim_pressed_ = false;
  pending_seconds_ = 0.0F;
  keys_.Clear();
  Draw();
}

ComponentType Highway::Step(float delta_seconds) {
  auto next = kType;
  const auto system_keys = ReadKeys();
  if (system_keys.abandon) {
    // As the original's Escape (0:bcb2): the bounty drops to 0, which ends the mission.
    game_.end_reason = EndReason::kBountyGone;
    mission_under_way_ = false;
    next = ComponentType::kMissionEnd;
  } else {
    ChangePhase(system_keys);
    if (phase_ == Phase::kDriving or phase_ == Phase::kCoasting) {
      next = RunTicks(delta_seconds);
      UpdateSounds();
    } else {
      // Silence while the game is paused, on the map or behind the spin-out picture.
      pending_seconds_ = 0.0F;
      StopSounds();
    }
    if (next == kType) {
      Draw();
    }
  }
  return next;
}

void Highway::ChangePhase(const SystemKeys& system_keys) {
  if (phase_ == Phase::kSpinOut) {
    // The picture stays until a key.
    phase_ = system_keys.any ? Phase::kDriving : Phase::kSpinOut;
  } else if (phase_ != Phase::kCoasting) {
    if (system_keys.map) {
      phase_ = phase_ == Phase::kMap ? Phase::kDriving : Phase::kMap;
    } else if (system_keys.pause and phase_ != Phase::kMap) {
      phase_ = phase_ == Phase::kPaused ? Phase::kDriving : Phase::kPaused;
    }
  }
}

ComponentType Highway::RunTicks(float delta_seconds) {
  auto next = kType;
  pending_seconds_ += delta_seconds;
  auto ticks = 0;
  while (pending_seconds_ >= Mission::kTickSeconds and ticks < kMaxTicksPerFrame and next == kType and
         (phase_ == Phase::kDriving or phase_ == Phase::kCoasting)) {
    next = Handle(mission_.Step(TakeCommands()));
    pending_seconds_ -= Mission::kTickSeconds;
    ++ticks;
  }
  // After a stall the rest is dropped: the game slows down instead of jumping.
  pending_seconds_ = std::min(pending_seconds_, Mission::kTickSeconds);
  return next;
}

void Highway::Play(EngineSound sound, std::size_t voice, float volume) {
  const auto& sample = assets_.Sound(sound);
  auto& output = audio_.GetVoice(voice);
  output.SetVolume(volume);
  output.SetStep(static_cast<float>(sample.rate_hz) / static_cast<float>(AudioEngine::kSampleRate));
  output.Start(sample.samples, sample.loop_start, std::size_t{sample.loop_start} + sample.loop_length);
}

void Highway::UpdateSounds() {
  const auto& player = mission_.GetPlayer();
  const auto speed = std::abs(player.speed);
  // The engine's note rises with the speed: the sample plays at its own rate at 100 units a frame.
  auto& engine = audio_.GetVoice(kEngineVoice);
  if (not engine.Active()) {
    Play(EngineSound::kEngine, kEngineVoice, kFullVolume);
  }
  const auto period = kEngineIdlePeriod - std::min(speed / kOriginalFramesPerSecond, kEngineFastest);
  engine.SetStep(static_cast<float>(assets_.Sound(EngineSound::kEngine).rate_hz) * kEngineSamplePeriod /
                 (period * static_cast<float>(AudioEngine::kSampleRate)));
  // The siren wails for as long as it is on.
  auto& siren = audio_.GetVoice(kSirenVoice);
  if (mission_.GetProgress().siren != siren.Active()) {
    if (siren.Active()) {
      siren.Stop();
    } else {
      Play(EngineSound::kSiren, kSirenVoice, kLoudVolume);
    }
  }
  // The tires squeal while the car slides at speed, and once, quietly, as it leaves the road.
  const auto off_road = mission_.GetCondition().off_road;
  if (speed >= kSkidSpeed and not audio_.GetVoice(kSkidVoice).Active()) {
    if (std::abs(player.slip) >= kSkidSlip) {
      Play(EngineSound::kSkid, kSkidVoice, kLoudVolume);
    } else if (off_road and not was_off_road_) {
      Play(EngineSound::kSkid, kSkidVoice, kQuietVolume);
    }
  }
  was_off_road_ = off_road;
}

void Highway::StopSounds() {
  for (const auto voice : {kEngineVoice, kSirenVoice, kEffectVoice, kSkidVoice}) {
    audio_.GetVoice(voice).Stop();
  }
}

ComponentType Highway::Handle(MissionEvents events) {
  auto next = kType;
  const auto happened = [events](MissionEvent event) {
    return events.test(std::to_underlying(event));
  };
  // A crash and a shot share a voice, and the crash comes first: a shot neither replaces it nor
  // cuts it short (the original's priorities, 5 for the crash and 3 for the shot).
  crash_sounding_ = crash_sounding_ and audio_.GetVoice(kEffectVoice).Active();
  if (happened(MissionEvent::kCrash)) {
    Play(EngineSound::kCrash, kEffectVoice, kLoudVolume);
    crash_sounding_ = true;
  } else if ((happened(MissionEvent::kShot) or happened(MissionEvent::kWindshieldHit)) and not crash_sounding_) {
    Play(EngineSound::kShot, kEffectVoice, kLoudVolume);
  }
  if (happened(MissionEvent::kMissionOver)) {
    phase_ = Phase::kCoasting;
  }
  const auto station = mission_.GetStation();
  if (happened(MissionEvent::kStationStop) and station) {
    game_.fuel = mission_.GetCondition().fuel;
    game_.tires = mission_.GetCondition().tires;
    game_.station_robbed = mission_.GetProgress().robbed.test(*station);
    next = ComponentType::kStation;
  } else if (happened(MissionEvent::kSpinOut)) {
    phase_ = Phase::kSpinOut;
    held_ = {};
  } else if (phase_ == Phase::kCoasting and mission_.GetPlayer().speed == 0.0F) {
    // The cars have rolled to a stop: the ending, with what is left of the bounty as the score.
    game_.end_reason = mission_.GetProgress().end_reason;
    game_.score = static_cast<std::uint32_t>(std::max(mission_.GetProgress().bounty, std::int32_t{0}));
    mission_under_way_ = false;
    next = ComponentType::kMissionEnd;
  }
  return next;
}

Highway::SystemKeys Highway::ReadKeys() {
  auto system_keys = SystemKeys{};
  while (const auto event = keys_.Next()) {
    const auto pressed = event->action == KeyAction::kPress;
    system_keys.any = system_keys.any or pressed;
    switch (event->key) {
      case Key::kS:
        siren_pressed_ = siren_pressed_ or pressed;
        break;
      case Key::kT:
        aim_pressed_ = aim_pressed_ or pressed;
        break;
      case Key::kLeft:
        held_.left = pressed;
        break;
      case Key::kRight:
        held_.right = pressed;
        break;
      case Key::kUp:
        held_.accelerate = pressed;
        break;
      case Key::kDown:
        held_.brake = pressed;
        break;
      case Key::kSpace:
        held_.fire = pressed;
        break;
      case Key::kP:
        system_keys.pause = system_keys.pause or pressed;
        break;
      case Key::kM:
        system_keys.map = system_keys.map or pressed;
        break;
      case Key::kEscape:
        system_keys.abandon = system_keys.abandon or pressed;
        break;
      default:
        break;
    }
  }
  return system_keys;
}

PlayerCommands Highway::TakeCommands() {
  // The wheel follows the held keys at kKeySteerRate, back to the middle when they are released.
  const auto target = static_cast<float>(held_.left) - static_cast<float>(held_.right);
  const auto step = kKeySteerRate * Mission::kTickSeconds;
  key_steer_ = target > key_steer_ ? std::min(key_steer_ + step, target) : std::max(key_steer_ - step, target);
  const auto throttle = static_cast<float>(held_.accelerate) - static_cast<float>(held_.brake);
  auto commands = PlayerCommands{.controls = {.steer = key_steer_, .throttle = throttle},
                                 .firing = held_.fire,
                                 .toggle_siren = siren_pressed_,
                                 .toggle_aim = aim_pressed_};
  siren_pressed_ = false;
  aim_pressed_ = false;
  return commands;
}

void Highway::Draw() {
  if (phase_ == Phase::kMap) {
    map_view_.Draw(mission_.GetPlayer().position, mission_.GetTarget().position);
  } else if (phase_ == Phase::kSpinOut) {
    screen_.DisableSplit();
    screen_.Palette(Viewport::kUpper).Reset();
    screen_.Palette(Viewport::kUpper).Overlay(assets_.Palette(EnginePalette::kSpinOut), 0);
    screen_.Blit(assets_.Picture(EnginePicture::kSpinOut), Point{});
  } else {
    driver_view_.Draw(mission_);
  }
}

}  // namespace hp2
