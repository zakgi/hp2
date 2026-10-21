#include "core/mission_end.hpp"

#include "core/engine_state.hpp"

#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>

namespace hp2 {

namespace {

constexpr auto kScoreModulus = std::uint32_t{100'000};
constexpr auto kDecimalBase = std::uint32_t{10};

}  // namespace

MissionEnd::Ending MissionEnd::GetEnding(EndReason reason) {
  auto ending =
      Ending{.picture = std::nullopt, .palette = std::nullopt, .text = {}, .text_cell = {}, .game_over = true};
  switch (reason) {
    case EndReason::kOutOfFuel:
      ending.picture = EnginePicture::kOutOfFuel;
      ending.palette = EnginePalette::kOutOfFuel;
      break;
    case EndReason::kStationsRobbed:
      ending.text = "ALL THE STATIONS HAVE BEEN ROBBED...";
      ending.text_cell = Point{.x = 2, .y = 22};
      break;
    case EndReason::kOverheated:
      ending.picture = EnginePicture::kOverheated;
      ending.palette = EnginePalette::kOverheated;
      break;
    case EndReason::kWrecked:
      ending.picture = EnginePicture::kWrecked;
      ending.palette = EnginePalette::kWrecked;
      break;
    case EndReason::kTiresGone:
      ending.picture = EnginePicture::kTiresGone;
      ending.palette = EnginePalette::kTiresGone;
      break;
    case EndReason::kShot:
      ending.text = "YOU HAVE BEEN SHOT...";
      ending.text_cell = Point{.x = 9, .y = 22};
      break;
    case EndReason::kArrest:
      ending.picture = EnginePicture::kArrest;
      ending.palette = EnginePalette::kArrest;
      ending.game_over = false;
      break;
    case EndReason::kBountyGone:
      break;
  }
  return ending;
}

// Lifecycle hooks use the instance-based ComponentLike protocol.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
void MissionEnd::OnEnter(EngineState& state) {
  state.scene.emplace<MissionEndState>();
  auto& mission_end = std::get<MissionEndState>(state.scene);
  state.keys.Clear();
  auto reason = state.mission.GetProgress().end_reason.value_or(EndReason::kBountyGone);
  // An arrest with nothing left of the bounty ends like a bounty run out.
  if (reason == EndReason::kArrest and state.game.score == 0) {
    reason = EndReason::kBountyGone;
  }
  mission_end.ending = GetEnding(reason);
  if (reason == EndReason::kArrest) {
    mission_end.new_career = state.game.MissionsLeft() == 0;
    if (mission_end.new_career) {
      mission_end.ending.text = kLastPosterText;
      mission_end.ending.text_cell = kLastPosterCell;
    }
  } else {
    state.game.score = 0;
    mission_end.new_career = true;
  }
  Enter(state, mission_end.ending.picture ? MissionEndStage::kPicture : MissionEndStage::kScore);
}

void MissionEnd::OnExit(EngineState& state) {
  state.scene.emplace<std::monostate>();
}

ComponentType MissionEnd::Step(EngineState& state, float delta_seconds) {
  auto& mission_end = std::get<MissionEndState>(state.scene);
  auto quit = false;
  auto proceed = false;
  while (const auto event = state.keys.Next()) {
    if (event->action == KeyAction::kPress) {
      quit = quit or event->key == Key::kEscape;
      proceed = proceed or event->key == Key::kSpace or event->key == Key::kEnter;
    }
  }
  const auto waiting =
      mission_end.stage == MissionEndStage::kPictureShown or mission_end.stage == MissionEndStage::kScoreShown;
  if ((waiting and proceed) or
      (not waiting and mission_end.stage != MissionEndStage::kDone and mission_end.actions.Empty())) {
    Enter(state, static_cast<MissionEndStage>(std::to_underlying(mission_end.stage) + 1));
  }
  mission_end.actions.Tick(delta_seconds);
  auto next = kType;
  if (quit) {
    next = ComponentType::kQuit;
  } else if (mission_end.stage == MissionEndStage::kDone) {
    if (mission_end.new_career) {
      state.game.ResetCareer();
    }
    state.game.mission.reset();
    state.mission.GetProgress().end_reason.reset();
    next = ComponentType::kOffice;
  }
  return next;
}

// NOLINTEND(readability-convert-member-functions-to-static)

void MissionEnd::Enter(EngineState& state, MissionEndStage stage) {
  auto& mission_end = std::get<MissionEndState>(state.scene);
  mission_end.stage = stage;
  switch (stage) {
    case MissionEndStage::kPicture: {
      const auto palette = state.assets.Palette(*mission_end.ending.palette);
      state.screen.DisableSplit();
      state.screen.Palette(Viewport::kUpper).Reset();
      state.screen.Clear(0);
      state.screen.Blit(state.assets.Picture(*mission_end.ending.picture), Point{});
      state.screen.Palette(Viewport::kUpper).Overlay(palette, 0);
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 0.0F, 1.0F, kFadeSeconds);
      break;
    }
    case MissionEndStage::kPictureOut:
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 1.0F, 0.0F, kFadeSeconds);
      break;
    case MissionEndStage::kScore:
      ComposeScore(state);
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 0.0F, 1.0F, kFadeSeconds);
      break;
    case MissionEndStage::kScoreOut:
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 1.0F, 0.0F, kFadeSeconds);
      break;
    case MissionEndStage::kPictureShown:
    case MissionEndStage::kScoreShown:
    case MissionEndStage::kDone:
      break;
  }
}

void MissionEnd::ComposeScore(EngineState& state) {
  auto& mission_end = std::get<MissionEndState>(state.scene);
  auto& palette = state.screen.Palette(Viewport::kUpper);
  state.screen.DisableSplit();
  palette.Reset();
  palette.Overlay(state.assets.Palette(EnginePalette::kScore), 0);
  state.screen.Clear(0);
  const auto& bank = state.assets.Bank(EngineBank::kScore);
  if (mission_end.ending.game_over) {
    state.screen.BlitMasked(bank.GetImage(kGameOverImage), kGameOverPosition);
  }
  state.screen.BlitMasked(bank.GetImage(kScoreLabelImage), kScoreLabelPosition);
  auto value = state.game.score % kScoreModulus;
  for (auto place = kScoreDigits; place > 0; --place) {
    const auto digit = value % kDecimalBase;
    value /= kDecimalBase;
    const auto digit_position =
        Point{.x = static_cast<std::int16_t>(kFirstDigitPosition.x + (kDigitAdvance * (place - 1))),
              .y = kFirstDigitPosition.y};
    state.screen.BlitMasked(bank.GetImage(kFirstDigitImage + digit), digit_position);
  }
  if (not mission_end.ending.text.empty()) {
    const auto& font = state.assets.Font(EngineFont::kLettre1);
    state.screen.DrawText(font, mission_end.ending.text,
                          TextCell(mission_end.ending.text_cell.x, mission_end.ending.text_cell.y), kTextIndexOffset);
  }
}

}  // namespace hp2
