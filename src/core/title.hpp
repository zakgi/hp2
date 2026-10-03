#pragma once

#include <cstddef>
#include <cstdint>

#include "core/action_stack.hpp"
#include "core/component.hpp"
#include "core/engine_assets.hpp"
#include "core/key_events.hpp"
#include "core/palette.hpp"
#include "core/screen.hpp"
#include "core/xor_animation.hpp"

namespace hp2 {

// The original's waits are busy loops on the NTSC machine the crack targets; their lengths here are
// counted from the instructions (68000 cycle counts), not measured. The music interrupt's share of
// the processor is not counted.
namespace original_timing {

inline constexpr auto kCpuHz = 7'159'090.0F;
// One vertical blank, which FlipScreens (1:0afa) waits for; NTSC, taken as 60 Hz.
inline constexpr auto kVerticalBlankSeconds = 1.0F / 60.0F;
// One taken `dbf` of a delay loop.
inline constexpr auto kDbfCycles = 10U;
// STScreenToPlanar (0:1124) and the reverse loop at 0:ac52: 200 rows of 20 x 4 word moves.
inline constexpr auto kScreenConversionCycles = 236'400U;
// CopyScreen (1:222c): 200 rows of 4 x 10-long movem pairs.
inline constexpr auto kScreenCopyCycles = 158'800U;

[[nodiscard]] constexpr float Seconds(std::uint32_t cycles) {
  return static_cast<float>(cycles) / kCpuHz;
}

// One fade level (main 0:a9f6 and its copies): two FlipScreens, then 65536 dbf.
inline constexpr auto kFadeStepSeconds = (2.0F * kVerticalBlankSeconds) + Seconds(65'536U * kDbfCycles);
// Title shown, before the animation (0:ac42-0:acce, then PlayDIF 0:106e up to its first
// frame): 16 x 65536 dbf, the conversion to Atari ST layout and back, a screen copy, 7 x 61441 dbf.
inline constexpr auto kAnimationLeadSeconds =
    Seconds((((16U * 65'536U) + (7U * 61'441U)) * kDbfCycles) + (2U * kScreenConversionCycles) + kScreenCopyCycles);

// One play list step of PlayDIF: ApplyDIFFrame (0:1102, 70 cycles a frame, 56 a run, 46 a
// word), the conversion to the displayed screen, then two loops of `delay` + 1 dbf.
[[nodiscard]] constexpr float AnimationStepSeconds(const XorFrame& frame, std::uint16_t delay) {
  constexpr auto kFrameCycles = 70U;
  constexpr auto kRunCycles = 56U;
  constexpr auto kWordCycles = 46U;
  const auto apply =
      kFrameCycles + (kRunCycles * static_cast<std::uint32_t>(frame.runs.size())) + (kWordCycles * frame.source_words);
  return Seconds(apply + kScreenConversionCycles + (2U * (std::uint32_t{delay} + 1U) * kDbfCycles));
}

}  // namespace original_timing

// Leaves the screen as it is for a while.
class Hold {
 public:
  explicit Hold(float seconds) : seconds_(seconds) {}

  void Init() { remaining_seconds_ = seconds_; }
  bool Tick(float delta_seconds) {
    remaining_seconds_ -= delta_seconds;
    return remaining_seconds_ <= 0.0F;
  }

 private:
  float seconds_;
  float remaining_seconds_{};
};

// FadePaletteList (0:0d06) stepped from `first_level` to `last_level`, one level per fade step
// and a step's wait after each, the first level shown at once.
class PaletteFade {
 public:
  PaletteFade(Screen& screen, PaletteProgram program, std::uint8_t first_level, std::uint8_t last_level)
      : screen_(screen), program_(program), first_level_(first_level), last_level_(last_level) {}

  void Init();
  bool Tick(float delta_seconds);

 private:
  Screen& screen_;
  PaletteProgram program_;
  std::uint8_t first_level_;
  std::uint8_t last_level_;
  std::uint8_t level_{};
  float remaining_seconds_{};
};

// PlayDIF (0:106e) from its first frame: each step XORs a frame into the screen, then waits.
class PlayAnimation {
 public:
  PlayAnimation(Screen& screen, XorAnimation animation) : screen_(screen), animation_(animation) {}

  void Init();
  bool Tick(float delta_seconds);

 private:
  Screen& screen_;
  XorAnimation animation_;
  std::size_t step_{};
  float remaining_seconds_{};
};

// Shows `program` at fade `level`: each viewport takes the segments in effect on its first row.
// Segments starting inside a viewport would need palette entries of their own; the presentation's
// lists have none.
void ShowPalette(Screen& screen, PaletteProgram program, std::uint8_t level);

// XORs `frame` into the screen.
void ApplyFrame(Screen& screen, const XorFrame& frame);

// The opening presentation (main, 0:a908-0:b05e; docs/game.md, "Program flow"): LOGO.CPV fades
// in, PRESENT.CPV fades in under the title palette, PRESENT.DIF plays over it, NAME.IMG adds the
// lettering. Space or Enter skips to the finished title; on the finished title they fade it out
// and leave. Escape quits at any time. The music (HIGHWAY.MUS) is not played yet. A state machine
// over the stages, as in the original's straight-line code: each stage composes its picture on
// entry and pushes its timed parts, and the next stage begins when the stack empties.
class Title {
 public:
  static constexpr ComponentType kType = ComponentType::kTitle;

  // Stands in for the original's disk loading behind the logo (PRESENT.CPV, PRESENT.DIF,
  // HIGHWAY.MUS), which the port does not have.
  static constexpr float kLogoHoldSeconds = 2.0F;

  // NAME.IMG images 2 and 3 (1-based, as BlitBob takes them) and where main draws them, masked,
  // in screen coordinates (0:ad06-0:ad88).
  static constexpr Point kLowerNameAt{.x = 10, .y = 189};
  static constexpr Point kRightNameAt{.x = 258, .y = 143};
  // Then the 8 x 8 cell at kCellFrom is copied to kCellTo (0:ad92), turning the lettering's
  // "LICENCE" into "LICENSE".
  static constexpr Point kCellFrom{.x = 240, .y = 192};
  static constexpr Point kCellTo{.x = 160, .y = 192};
  static constexpr std::uint16_t kCellSize = 8;

  Title(const EngineAssets& assets, Screen& screen, KeyEvents& keys) : assets_(assets), screen_(screen), keys_(keys) {}

  void OnEnter();
  void OnExit();
  ComponentType Step(float delta_seconds);

 private:
  enum class Stage : std::uint8_t {
    kLogo,       // LOGO.CPV fades in and stays
    kLogoGone,   // the original clears the logo, then fades its palette out on the black screen
    kTitle,      // PRESENT.CPV fades in and stays
    kAnimation,  // PRESENT.DIF
    kNames,      // the lettering; waits for Space or Enter
    kFadeOut,    // the title palette fades out
    kDone,
  };

  static constexpr std::size_t kActionCapacity = 4;
  using Actions = ActionStack<kActionCapacity, Hold, PaletteFade, PlayAnimation>;

  // Enters `stage`: composes its picture and pushes its timed actions.
  void Enter(Stage stage);
  [[nodiscard]] Stage Next() const;
  // The title picture with the screen split where the title palette's second segment starts.
  void ComposeTitle();
  void DrawNames();
  // Abandons the presentation for the finished title: picture, every animation step, lettering.
  void Finish();

  const EngineAssets& assets_;
  Screen& screen_;
  KeyEvents& keys_;
  Actions actions_;
  Stage stage_{Stage::kDone};
};

static_assert(ComponentLike<Title>);

}  // namespace hp2
