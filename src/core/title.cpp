#include "core/title.hpp"

#include "core/engine_state.hpp"

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

// Lifecycle hooks use the instance-based ComponentLike protocol.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
void Title::OnEnter(EngineState& state) {
  state.scene.emplace<TitleState>();
  state.keys.Clear();
  Enter(state, TitleStage::Logo);
}

void Title::OnExit(EngineState& state) {
  state.audio.StopMusic();
  state.scene.emplace<std::monostate>();
}

TitleStage Title::Next(const EngineState& state) {
  const auto& title = std::get<TitleState>(state.scene);
  return title.stage == TitleStage::Done ? TitleStage::Done
                                         : static_cast<TitleStage>(std::to_underlying(title.stage) + 1);
}

ComponentType Title::Step(EngineState& state, float delta_seconds) {
  auto& title = std::get<TitleState>(state.scene);
  auto quit = false;
  auto proceed = false;
  while (const auto event = state.keys.Next()) {
    if (event->action == KeyAction::Press) {
      quit = quit or event->key == Key::Escape;
      proceed = proceed or event->key == Key::Space or event->key == Key::Enter;
    }
  }
  if (proceed and title.stage < TitleStage::Names) {
    Finish(state);
  } else if (proceed and title.stage == TitleStage::Names) {
    Enter(state, TitleStage::FadeOut);
  }
  // The lettering stays until a keystroke; every other stage ends when its actions have run.
  if (title.actions.Empty() and title.stage != TitleStage::Names and title.stage != TitleStage::Done) {
    Enter(state, Next(state));
  }
  title.actions.Tick(delta_seconds);
  auto next = kType;
  if (quit) {
    next = ComponentType::Quit;
  } else if (title.stage == TitleStage::Done) {
    next = ComponentType::Office;
  }
  return next;
}

// NOLINTEND(readability-convert-member-functions-to-static)

// The stack runs top first, so each stage pushes its actions in reverse order. The capacity
// covers the largest stage; a refused push would only shorten the presentation.
void Title::Enter(EngineState& state, TitleStage stage) {
  auto& title = std::get<TitleState>(state.scene);
  title.stage = stage;
  switch (stage) {
    case TitleStage::Logo:
      state.screen.DisableSplit();
      state.screen.Palette(Viewport::Upper).Reset();
      state.screen.Clear(0);
      state.screen.Blit(state.assets.Picture(EnginePicture::Logo), Point{});
      state.screen.Palette(Viewport::Upper).Overlay(state.assets.Palette(EnginePalette::Logo), 0);
      std::ignore = title.actions.Push<Hold>(kLogoHoldSeconds);
      std::ignore = title.actions.Push<Fade>(state.screen, 0.0F, 1.0F, kFadeSeconds);
      break;

    case TitleStage::LogoGone:
      std::ignore = title.actions.Push<Fade>(state.screen, 1.0F, 0.0F, kFadeSeconds);
      break;

    case TitleStage::Title:
      ComposeTitle(state);
      state.audio.PlayMusic(state.assets.title_music);
      std::ignore = title.actions.Push<Fade>(state.screen, 0.0F, 1.0F, kFadeSeconds);
      break;

    case TitleStage::Animation:
      std::ignore = title.actions.Push<PlayAnimation>(state.screen, state.assets.title_animation);
      std::ignore = title.actions.Push<Hold>(original_timing::kAnimationLeadSeconds);
      break;

    case TitleStage::Names:
      DrawNames(state);
      break;

    case TitleStage::FadeOut:
      state.audio.StopMusic();
      std::ignore = title.actions.Push<Fade>(state.screen, 1.0F, 0.0F, kFadeSeconds);
      break;

    case TitleStage::Done:
      break;
  }
}

void Title::ComposeTitle(EngineState& state) {
  auto& palette = state.screen.Palette(Viewport::Upper);
  state.screen.DisableSplit();
  palette.Reset();
  palette.Overlay(state.assets.Palette(EnginePalette::Title), 0);
  state.screen.Clear(0);
  const auto picture = state.assets.Picture(EnginePicture::Title);
  if (picture.height > kSkyRows) {
    const auto sky_pixels = std::size_t{kSkyRows} * picture.width;
    state.screen.Blit(ImageView{.width = picture.width, .height = kSkyRows, .pixels = picture.pixels.first(sky_pixels)},
                      Point{});
    state.screen.Blit(ImageView{.width = picture.width,
                                .height = static_cast<std::uint16_t>(picture.height - kSkyRows),
                                .pixels = picture.pixels.subspan(sky_pixels)},
                      Point{.x = 0, .y = static_cast<std::int16_t>(kSkyRows)}, kPictureIndexOffset);
  }
}

// Everything here lies below the sky, so it is drawn with the picture's offset.
void Title::DrawNames(EngineState& state) {
  const auto& names = state.assets.Bank(EngineBank::Names);
  if (names.sprites.size() >= 3) {
    state.screen.BlitMasked(names.GetImage(1), kLowerNamePosition, kPictureIndexOffset);
    state.screen.BlitMasked(names.GetImage(2), kRightNamePosition, kPictureIndexOffset);
  }
  state.screen.Copy(kCellFrom, kCellTo, kCellSize, kCellSize);
}

void Title::Finish(EngineState& state) {
  auto& title = std::get<TitleState>(state.scene);
  title.actions.Clear();
  ComposeTitle(state);
  const auto frames = state.assets.title_animation.frames;
  for (const auto& step : state.assets.title_animation.steps) {
    if (step.frame < frames.size()) {
      state.screen.ApplyFrame(state.assets.title_animation, frames[step.frame]);
    }
  }
  if (not state.audio.MusicPlaying()) {
    state.audio.PlayMusic(state.assets.title_music);
  }
  Enter(state, TitleStage::Names);
}

}  // namespace hp2
