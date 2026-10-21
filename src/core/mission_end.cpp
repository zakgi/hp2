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
    case EndReason::OutOfFuel:
      ending.picture = EnginePicture::OutOfFuel;
      ending.palette = EnginePalette::OutOfFuel;
      break;
    case EndReason::StationsRobbed:
      ending.text = "ALL THE STATIONS HAVE BEEN ROBBED...";
      ending.text_cell = Point{.x = 2, .y = 22};
      break;
    case EndReason::Overheated:
      ending.picture = EnginePicture::Overheated;
      ending.palette = EnginePalette::Overheated;
      break;
    case EndReason::Wrecked:
      ending.picture = EnginePicture::Wrecked;
      ending.palette = EnginePalette::Wrecked;
      break;
    case EndReason::TiresGone:
      ending.picture = EnginePicture::TiresGone;
      ending.palette = EnginePalette::TiresGone;
      break;
    case EndReason::Shot:
      ending.text = "YOU HAVE BEEN SHOT...";
      ending.text_cell = Point{.x = 9, .y = 22};
      break;
    case EndReason::Arrest:
      ending.picture = EnginePicture::Arrest;
      ending.palette = EnginePalette::Arrest;
      ending.game_over = false;
      break;
    case EndReason::BountyGone:
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
  auto reason = state.mission.GetProgress().end_reason.value_or(EndReason::BountyGone);
  // An arrest with nothing left of the bounty ends like a bounty run out.
  if (reason == EndReason::Arrest and state.game.score == 0) {
    reason = EndReason::BountyGone;
  }
  mission_end.ending = GetEnding(reason);
  if (reason == EndReason::Arrest) {
    mission_end.new_career = state.game.MissionsLeft() == 0;
    if (mission_end.new_career) {
      mission_end.ending.text = kLastPosterText;
      mission_end.ending.text_cell = kLastPosterCell;
    }
  } else {
    state.game.score = 0;
    mission_end.new_career = true;
  }
  Enter(state, mission_end.ending.picture ? MissionEndStage::Picture : MissionEndStage::Score);
}

void MissionEnd::OnExit(EngineState& state) {
  state.scene.emplace<std::monostate>();
}

ComponentType MissionEnd::Step(EngineState& state, float delta_seconds) {
  auto& mission_end = std::get<MissionEndState>(state.scene);
  auto quit = false;
  auto proceed = false;
  while (const auto event = state.keys.Next()) {
    if (event->action == KeyAction::Press) {
      quit = quit or event->key == Key::Escape;
      proceed = proceed or event->key == Key::Space or event->key == Key::Enter;
    }
  }
  const auto waiting =
      mission_end.stage == MissionEndStage::PictureShown or mission_end.stage == MissionEndStage::ScoreShown;
  if ((waiting and proceed) or
      (not waiting and mission_end.stage != MissionEndStage::Done and mission_end.actions.Empty())) {
    Enter(state, static_cast<MissionEndStage>(std::to_underlying(mission_end.stage) + 1));
  }
  mission_end.actions.Tick(delta_seconds);
  auto next = kType;
  if (quit) {
    next = ComponentType::Quit;
  } else if (mission_end.stage == MissionEndStage::Done) {
    if (mission_end.new_career) {
      state.game.ResetCareer();
    }
    state.game.mission.reset();
    state.mission.GetProgress().end_reason.reset();
    next = ComponentType::Office;
  }
  return next;
}

// NOLINTEND(readability-convert-member-functions-to-static)

void MissionEnd::Enter(EngineState& state, MissionEndStage stage) {
  auto& mission_end = std::get<MissionEndState>(state.scene);
  mission_end.stage = stage;
  switch (stage) {
    case MissionEndStage::Picture: {
      const auto palette = state.assets.Palette(*mission_end.ending.palette);
      state.screen.DisableSplit();
      state.screen.Palette(Viewport::Upper).Reset();
      state.screen.Clear(0);
      state.screen.Blit(state.assets.Picture(*mission_end.ending.picture), Point{});
      state.screen.Palette(Viewport::Upper).Overlay(palette, 0);
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 0.0F, 1.0F, kFadeSeconds);
      break;
    }
    case MissionEndStage::PictureOut:
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 1.0F, 0.0F, kFadeSeconds);
      break;
    case MissionEndStage::Score:
      ComposeScore(state);
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 0.0F, 1.0F, kFadeSeconds);
      break;
    case MissionEndStage::ScoreOut:
      std::ignore = mission_end.actions.Push<Fade>(state.screen, 1.0F, 0.0F, kFadeSeconds);
      break;
    case MissionEndStage::PictureShown:
    case MissionEndStage::ScoreShown:
    case MissionEndStage::Done:
      break;
  }
}

void MissionEnd::ComposeScore(EngineState& state) {
  auto& mission_end = std::get<MissionEndState>(state.scene);
  auto& palette = state.screen.Palette(Viewport::Upper);
  state.screen.DisableSplit();
  palette.Reset();
  palette.Overlay(state.assets.Palette(EnginePalette::Score), 0);
  state.screen.Clear(0);
  const auto& bank = state.assets.Bank(EngineBank::Score);
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
    const auto& font = state.assets.Font(EngineFont::Lettre1);
    state.screen.DrawText(font, mission_end.ending.text,
                          TextCell(mission_end.ending.text_cell.x, mission_end.ending.text_cell.y), kTextIndexOffset);
  }
}

}  // namespace hp2
