#include "core/title.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>

#include "core/image_view.hpp"

namespace hp2 {

void PlayAnimation::Init() {
  step_ = 0;
  remaining_seconds_ = 0.0F;
}

// Steps are never skipped, however long the tick: each frame is a delta on the one before.
bool PlayAnimation::Tick(float delta_seconds) {
  remaining_seconds_ -= delta_seconds;
  const auto steps = animation_.steps;
  while (remaining_seconds_ <= 0.0F and step_ < steps.size()) {
    const auto& step = steps[step_];
    if (step.frame < animation_.frames.size()) {
      const auto& frame = animation_.frames[step.frame];
      screen_.ApplyFrame(animation_, frame);
      remaining_seconds_ += original_timing::AnimationStepSeconds(frame, step.delay);
    }
    ++step_;
  }
  return step_ == steps.size() and remaining_seconds_ <= 0.0F;
}

void Title::OnEnter() {
  actions_.Clear();
  keys_.Clear();
  Enter(Stage::kLogo);
}

void Title::OnExit() {
  actions_.Clear();
  audio_.StopMusic();
  stage_ = Stage::kDone;
}

Title::Stage Title::Next() const {
  return stage_ == Stage::kDone ? Stage::kDone : static_cast<Stage>(std::to_underlying(stage_) + 1);
}

ComponentType Title::Step(float delta_seconds) {
  auto quit = false;
  auto proceed = false;
  while (const auto event = keys_.Next()) {
    if (event->action == KeyAction::kPress) {
      quit = quit or event->key == Key::kEscape;
      proceed = proceed or event->key == Key::kSpace or event->key == Key::kEnter;
    }
  }
  if (proceed and stage_ < Stage::kNames) {
    Finish();
  } else if (proceed and stage_ == Stage::kNames) {
    Enter(Stage::kFadeOut);
  }
  // The lettering stays until a keystroke; every other stage ends when its actions have run.
  if (actions_.Empty() and stage_ != Stage::kNames and stage_ != Stage::kDone) {
    Enter(Next());
  }
  actions_.Tick(delta_seconds);
  auto next = kType;
  if (quit) {
    next = ComponentType::kQuit;
  } else if (stage_ == Stage::kDone) {
    next = ComponentType::kOffice;
  }
  return next;
}

// The stack runs top first, so each stage pushes its actions in reverse order. The capacity
// covers the largest stage; a refused push would only shorten the presentation.
void Title::Enter(Stage stage) {
  stage_ = stage;
  switch (stage) {
    case Stage::kLogo:
      screen_.DisableSplit();
      screen_.Palette(Viewport::kUpper).Reset();
      screen_.Clear(0);
      screen_.Blit(assets_.Picture(EnginePicture::kLogo), Point{});
      screen_.Palette(Viewport::kUpper).Overlay(assets_.Palette(EnginePalette::kLogo), 0);
      std::ignore = actions_.Push<Hold>(kLogoHoldSeconds);
      std::ignore = actions_.Push<Fade>(screen_, 0.0F, 1.0F, kFadeSeconds);
      break;

    case Stage::kLogoGone:
      std::ignore = actions_.Push<Fade>(screen_, 1.0F, 0.0F, kFadeSeconds);
      break;

    case Stage::kTitle:
      ComposeTitle();
      audio_.PlayMusic(assets_.title_music);
      std::ignore = actions_.Push<Fade>(screen_, 0.0F, 1.0F, kFadeSeconds);
      break;

    case Stage::kAnimation:
      std::ignore = actions_.Push<PlayAnimation>(screen_, assets_.title_animation);
      std::ignore = actions_.Push<Hold>(original_timing::kAnimationLeadSeconds);
      break;

    case Stage::kNames:
      DrawNames();
      break;

    case Stage::kFadeOut:
      audio_.StopMusic();
      std::ignore = actions_.Push<Fade>(screen_, 1.0F, 0.0F, kFadeSeconds);
      break;

    case Stage::kDone:
      break;
  }
}

void Title::ComposeTitle() {
  auto& palette = screen_.Palette(Viewport::kUpper);
  screen_.DisableSplit();
  palette.Reset();
  palette.Overlay(assets_.Palette(EnginePalette::kTitle), 0);
  screen_.Clear(0);
  const auto picture = assets_.Picture(EnginePicture::kTitle);
  if (picture.height > kSkyRows) {
    const auto sky_pixels = std::size_t{kSkyRows} * picture.width;
    screen_.Blit(ImageView{.width = picture.width, .height = kSkyRows, .pixels = picture.pixels.first(sky_pixels)},
                 Point{});
    screen_.Blit(ImageView{.width = picture.width,
                           .height = static_cast<std::uint16_t>(picture.height - kSkyRows),
                           .pixels = picture.pixels.subspan(sky_pixels)},
                 Point{.x = 0, .y = static_cast<std::int16_t>(kSkyRows)}, kPictureIndexOffset);
  }
}

// Everything here lies below the sky, so it is drawn with the picture's offset.
void Title::DrawNames() {
  const auto& names = assets_.Bank(EngineBank::kNames);
  if (names.sprites.size() >= 3) {
    screen_.BlitMasked(names.GetImage(1), kLowerNamePosition, kPictureIndexOffset);
    screen_.BlitMasked(names.GetImage(2), kRightNamePosition, kPictureIndexOffset);
  }
  screen_.Copy(kCellFrom, kCellTo, kCellSize, kCellSize);
}

void Title::Finish() {
  actions_.Clear();
  ComposeTitle();
  const auto frames = assets_.title_animation.frames;
  for (const auto& step : assets_.title_animation.steps) {
    if (step.frame < frames.size()) {
      screen_.ApplyFrame(assets_.title_animation, frames[step.frame]);
    }
  }
  if (not audio_.MusicPlaying()) {
    audio_.PlayMusic(assets_.title_music);
  }
  Enter(Stage::kNames);
}

}  // namespace hp2
