#include "core/highway.hpp"

#include <algorithm>
#include <cstdint>

#include "core/map_view.hpp"

namespace hp2 {

Highway::Highway(const EngineAssets& assets, Screen& screen, KeyEvents& keys, AudioEngine& audio, GameState& game,
                 std::uint64_t seed)
    : assets_(assets),
      screen_(screen),
      keys_(keys),
      audio_(audio),
      game_(game),
      mission_(Road{assets.road_map, assets.road_shapes, assets.scenery}),
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
  auto abandon = false;
  ReadKeys(abandon);
  pending_seconds_ += delta_seconds;
  auto ticks = 0;
  while (pending_seconds_ >= Mission::kTickSeconds and ticks < kMaxTicksPerFrame) {
    mission_.Step(TakeCommands());
    pending_seconds_ -= Mission::kTickSeconds;
    ++ticks;
  }
  // After a stall the rest is dropped: the game slows down instead of jumping.
  pending_seconds_ = std::min(pending_seconds_, Mission::kTickSeconds);
  Draw();
  return abandon ? ComponentType::kQuit : kType;
}

void Highway::ReadKeys(bool& abandon) {
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
      case Key::kEscape:
        abandon = abandon or pressed;
        break;
      default:
        break;
    }
  }
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

// Until the driver's view exists, the road shows as the live map.
void Highway::Draw() {
  DrawMap(screen_, mission_.GetRoad(), mission_.GetPlayer().position, mission_.GetTarget().position);
}

}  // namespace hp2
