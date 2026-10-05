#include "core/highway.hpp"

#include <algorithm>
#include <cstdint>

namespace hp2 {

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
  if (not mission_under_way_) {
    const auto seed = (std::uint64_t{seeds_.Next()} << 32U) | seeds_.Next();
    mission_.Start(kMissions[game_.mission.value_or(0)], policies_, seed);
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
    if (system_keys.map) {
      phase_ = phase_ == Phase::kMap ? Phase::kDriving : Phase::kMap;
    } else if (system_keys.pause and phase_ != Phase::kMap) {
      phase_ = phase_ == Phase::kPaused ? Phase::kDriving : Phase::kPaused;
    }
    if (phase_ == Phase::kDriving) {
      RunTicks(delta_seconds);
    } else {
      pending_seconds_ = 0.0F;
    }
    Draw();
  }
  return next;
}

void Highway::RunTicks(float delta_seconds) {
  pending_seconds_ += delta_seconds;
  auto ticks = 0;
  while (pending_seconds_ >= Mission::kTickSeconds and ticks < kMaxTicksPerFrame) {
    mission_.Step(TakeCommands());
    pending_seconds_ -= Mission::kTickSeconds;
    ++ticks;
  }
  // After a stall the rest is dropped: the game slows down instead of jumping.
  pending_seconds_ = std::min(pending_seconds_, Mission::kTickSeconds);
}

Highway::SystemKeys Highway::ReadKeys() {
  auto system_keys = SystemKeys{};
  while (const auto event = keys_.Next()) {
    const auto pressed = event->action == KeyAction::kPress;
    switch (event->key) {
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
  } else {
    driver_view_.Draw(mission_);
  }
}

}  // namespace hp2
