#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "core/action_stack.hpp"
#include "core/component.hpp"
#include "core/engine_assets.hpp"
#include "core/game_state.hpp"
#include "core/key_events.hpp"
#include "core/presentation.hpp"
#include "core/screen.hpp"

namespace hp2 {

// The end of a mission (CheckMissionEnd, 0:6008; docs/game.md, "Scenes and endings"): the picture
// of what happened, if there is one, then the score screen, each faded in, held until Space or
// Enter and faded out; then the office. An arrest keeps the score and the career, unless it was the
// last poster; every other ending clears the score and starts a new career. Escape quits.
class MissionEnd {
 public:
  static constexpr ComponentType kType = ComponentType::kMissionEnd;

  // How an ending shows (the handlers listed in work/analysis/frontend.md, section 4).
  struct Ending {
    std::optional<EnginePicture> picture;
    std::optional<EnginePalette> palette;
    std::string_view text;
    Point text_cell;
    bool game_over;
  };

  // GAME_SCO.IMG, 0-based: GAME OVER, SCORE :, then the digits 0-9 (DrawScore 0:762c, DrawGameOver
  // 0:76f8).
  static constexpr std::size_t kGameOverImage = 0;
  static constexpr std::size_t kScoreLabelImage = 1;
  static constexpr std::size_t kFirstDigitImage = 2;
  static constexpr Point kGameOverPosition{.x = 32, .y = 20};
  static constexpr Point kScoreLabelPosition{.x = 55, .y = 100};
  static constexpr Point kFirstDigitPosition{.x = 172, .y = 100};
  static constexpr std::int16_t kDigitAdvance = 19;
  static constexpr std::size_t kScoreDigits = 5;
  static constexpr std::string_view kLastPosterText = "YOUR MISSION IS OVER...";
  static constexpr Point kLastPosterCell{.x = 8, .y = 22};
  // The score palette changes at row 168, below everything but the ending's text: its second
  // segment takes the entries from kTextIndexOffset on, and the text is drawn with that offset.
  static constexpr std::uint8_t kTextIndexOffset = 16;

  MissionEnd(const EngineAssets& assets, Screen& screen, KeyEvents& keys, GameState& game)
      : assets_(assets), screen_(screen), keys_(keys), game_(game) {}

  void OnEnter();
  void OnExit();
  ComponentType Step(float delta_seconds);

  // How `reason` is shown.
  [[nodiscard]] static Ending GetEnding(EndReason reason);

 private:
  enum class Stage : std::uint8_t { kPicture, kPictureShown, kPictureOut, kScore, kScoreShown, kScoreOut, kDone };

  static constexpr std::size_t kActionCapacity = 2;
  using Actions = ActionStack<kActionCapacity, Fade>;

  void Enter(Stage stage);
  void ComposeScore();

  const EngineAssets& assets_;
  Screen& screen_;
  KeyEvents& keys_;
  GameState& game_;
  Actions actions_;
  Stage stage_{Stage::kDone};
  Ending ending_{};
  bool new_career_{};
};

static_assert(ComponentLike<MissionEnd>);

}  // namespace hp2
