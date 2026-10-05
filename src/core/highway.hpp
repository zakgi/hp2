#pragma once

#include <cstdint>

#include "core/audio_engine.hpp"
#include "core/component.hpp"
#include "core/driver_view.hpp"
#include "core/engine_assets.hpp"
#include "core/game_state.hpp"
#include "core/key_events.hpp"
#include "core/map_view.hpp"
#include "core/mission.hpp"
#include "core/screen.hpp"

namespace hp2 {

// The mission on the road, the original's main loop (0:ba7c): the keys become commands for the
// mission, which runs whole ticks of the elapsed time; its events become sounds and scenes, and
// the stop at a station or the end of the mission hands over to Station or MissionEnd. The arrows
// steer, accelerate and brake; Space fires while aiming; S switches the siren, T the gun, P pauses,
// M shows the map; Escape abandons the mission (the bounty drops to 0).
class Highway {
 public:
  static constexpr ComponentType kType = ComponentType::kHighway;
  // Ticks run per frame at most: after a longer stall the game slows down instead of jumping.
  static constexpr int kMaxTicksPerFrame = 4;
  // How fast held keys turn the wheel, full lock per second; the original turns it 6 of 60 a frame
  // (provisional: at 20 frames a second).
  static constexpr float kKeySteerRate = 2.0F;

  // `seed` comes from the platform at boot (TRNG or std::random_device).
  Highway(const EngineAssets& assets, Screen& screen, KeyEvents& keys, AudioEngine& audio, GameState& game,
          std::uint64_t seed);

  // The voices of the sounds, the original's channels (UpdateSoundEffects, 0:1162): the engine and
  // the skid on the left, the siren and the shots and crashes on the right.
  static constexpr std::size_t kEngineVoice = 0;
  static constexpr std::size_t kSirenVoice = 1;
  static constexpr std::size_t kEffectVoice = 2;
  static constexpr std::size_t kSkidVoice = 3;

  // Starts the mission chosen in the office, or resumes the one under way after the station.
  void OnEnter();
  void OnExit() { StopSounds(); }
  ComponentType Step(float delta_seconds);

  [[nodiscard]] const Mission& GetMission() const { return mission_; }

 private:
  enum class Phase : std::uint8_t {
    kDriving,
    kPaused,    // P
    kMap,       // M: paused on the road map, with the player's car and the criminal's
    kSpinOut,   // PAGE_F1 until a key, then on
    kCoasting,  // the mission is over: the cars roll to a stop before the ending (0:6008)
  };

  // Keys held, followed through their presses and releases.
  struct HeldKeys {
    bool left{};
    bool right{};
    bool accelerate{};
    bool brake{};
    bool fire{};
  };

  // The system keys pressed since the last frame.
  struct SystemKeys {
    bool pause{};    // P
    bool map{};      // M
    bool abandon{};  // Escape
    bool any{};      // any key at all
  };

  // Follows the pending keystrokes into the held keys; returns the system keys pressed.
  [[nodiscard]] SystemKeys ReadKeys();
  // What the system keys do to the phase: P pauses and M shows the map while driving, any key
  // leaves the spin-out picture.
  void ChangePhase(const SystemKeys& system_keys);
  // Runs the whole ticks `delta_seconds` covers, at most kMaxTicksPerFrame, up to the tick that
  // leaves the road for a picture or another component; returns the component to go on with.
  [[nodiscard]] ComponentType RunTicks(float delta_seconds);
  // The commands for the next tick; single presses count once.
  [[nodiscard]] PlayerCommands TakeCommands();
  // Sounds, phase changes and the component to switch to for one tick's events.
  [[nodiscard]] ComponentType Handle(MissionEvents events);
  // Starts `sound` on `voice` at `volume`, 0..1; a sound with a loop goes on until stopped.
  void Play(EngineSound sound, std::size_t voice, float volume);
  // The sounds that follow the car while it drives: the engine's note, the siren, the skid.
  void UpdateSounds();
  void StopSounds();
  void Draw();

  const EngineAssets& assets_;
  Screen& screen_;
  KeyEvents& keys_;
  AudioEngine& audio_;
  GameState& game_;
  Mission mission_;
  DriverView driver_view_;
  MapView map_view_;
  AiPolicies policies_;
  // Each mission's seed is drawn from it.
  Random seeds_;
  Phase phase_{Phase::kDriving};
  HeldKeys held_;
  // The wheel as the held keys have turned it, -1 full right .. 1 full left.
  float key_steer_{};
  // Presses not yet passed to a tick.
  bool siren_pressed_{};
  bool aim_pressed_{};
  // Elapsed time not yet simulated.
  float pending_seconds_{};
  bool mission_under_way_{};
  // Off the road at the last frame: leaving it at speed makes the tires squeal once.
  bool was_off_road_{};
  // The crash's sound is still on the voice it shares with the shots.
  bool crash_sounding_{};
};

static_assert(ComponentLike<Highway>);

}  // namespace hp2
