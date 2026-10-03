#include "core/mission_end.hpp"

#include <cstddef>
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
    case EndReason::kTyresGone:
      ending.picture = EnginePicture::kTyresGone;
      ending.palette = EnginePalette::kTyresGone;
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

void MissionEnd::OnEnter() {
  actions_.Clear();
  keys_.Clear();
  auto reason = game_.end_reason.value_or(EndReason::kBountyGone);
  // An arrest with nothing left of the bounty ends like a bounty run out.
  if (reason == EndReason::kArrest and game_.score == 0) {
    reason = EndReason::kBountyGone;
  }
  ending_ = GetEnding(reason);
  if (reason == EndReason::kArrest) {
    new_career_ = game_.MissionsLeft() == 0;
    if (new_career_) {
      ending_.text = kLastPosterText;
      ending_.text_cell = kLastPosterCell;
    }
  } else {
    game_.score = 0;
    new_career_ = true;
  }
  Enter(ending_.picture ? Stage::kPicture : Stage::kScore);
}

void MissionEnd::OnExit() {
  actions_.Clear();
  stage_ = Stage::kDone;
}

ComponentType MissionEnd::Step(float delta_seconds) {
  auto quit = false;
  auto proceed = false;
  while (const auto event = keys_.Next()) {
    if (event->action == KeyAction::kPress) {
      quit = quit or event->key == Key::kEscape;
      proceed = proceed or event->key == Key::kSpace or event->key == Key::kEnter;
    }
  }
  const auto waiting = stage_ == Stage::kPictureShown or stage_ == Stage::kScoreShown;
  if ((waiting and proceed) or (not waiting and stage_ != Stage::kDone and actions_.Empty())) {
    Enter(static_cast<Stage>(std::to_underlying(stage_) + 1));
  }
  actions_.Tick(delta_seconds);
  auto next = kType;
  if (quit) {
    next = ComponentType::kQuit;
  } else if (stage_ == Stage::kDone) {
    if (new_career_) {
      game_.ResetCareer();
    }
    game_.mission.reset();
    game_.end_reason.reset();
    next = ComponentType::kOffice;
  }
  return next;
}

void MissionEnd::Enter(Stage stage) {
  stage_ = stage;
  const auto score_palette = assets_.Palette(EnginePalette::kScore);
  switch (stage) {
    case Stage::kPicture: {
      const auto palette = assets_.Palette(*ending_.palette);
      screen_.DisableSplit();
      screen_.Palette(Viewport::kUpper).Reset();
      screen_.Clear(0);
      screen_.Blit(assets_.Picture(*ending_.picture), Point{});
      std::ignore = actions_.Push<PaletteFade>(screen_, palette, kDarkest, kFullBrightness, kFadeStepSeconds);
      break;
    }
    case Stage::kPictureOut:
      std::ignore = actions_.Push<PaletteFade>(screen_, assets_.Palette(*ending_.palette), kFullBrightness, kDarkest,
                                               kFadeStepSeconds);
      break;
    case Stage::kScore:
      ComposeScore();
      std::ignore = actions_.Push<PaletteFade>(screen_, score_palette, kDarkest, kFullBrightness, kFadeStepSeconds);
      break;
    case Stage::kScoreOut:
      std::ignore = actions_.Push<PaletteFade>(screen_, score_palette, kFullBrightness, kDarkest, kFadeStepSeconds);
      break;
    case Stage::kPictureShown:
    case Stage::kScoreShown:
    case Stage::kDone:
      break;
  }
}

// The score screen's palette changes at row 168, so the screen splits there.
void MissionEnd::ComposeScore() {
  const auto palette = assets_.Palette(EnginePalette::kScore);
  screen_.EnableSplit(palette.size() > 1 ? palette[1].first_row : std::uint16_t{0});
  for (const auto viewport : {Viewport::kUpper, Viewport::kLower}) {
    screen_.Palette(viewport).Reset();
    screen_.SetViewport(viewport);
    screen_.Clear(0);
  }
  const auto& bank = assets_.Bank(EngineBank::kScore);
  if (ending_.game_over) {
    DrawAcrossViewports(screen_, kGameOverAt,
                        [&](Point origin) { screen_.BlitMasked(bank.GetImage(kGameOverImage), origin); });
  }
  DrawAcrossViewports(screen_, kScoreLabelAt,
                      [&](Point origin) { screen_.BlitMasked(bank.GetImage(kScoreLabelImage), origin); });
  auto value = game_.score % kScoreModulus;
  for (auto place = kScoreDigits; place > 0; --place) {
    const auto digit = value % kDecimalBase;
    value /= kDecimalBase;
    const auto digit_at =
        Point{.x = static_cast<std::int16_t>(kFirstDigitAt.x + (kDigitAdvance * (place - 1))), .y = kFirstDigitAt.y};
    DrawAcrossViewports(screen_, digit_at,
                        [&](Point origin) { screen_.BlitMasked(bank.GetImage(kFirstDigitImage + digit), origin); });
  }
  if (not ending_.text.empty()) {
    const auto& font = assets_.Font(EngineFont::kLettre1);
    DrawAcrossViewports(screen_, TextCell(ending_.text_cell.x, ending_.text_cell.y),
                        [&](Point origin) { DrawText(screen_, font, ending_.text, origin); });
  }
}

}  // namespace hp2
