#include "core/office.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hp2 {

namespace {

enum Arrow : std::uint8_t { kLeftArrow, kRightArrow, kUpArrow, kDownArrow };

}  // namespace

Point Office::Pointer() const {
  return Point{.x = static_cast<std::int16_t>(pointer_x_), .y = static_cast<std::int16_t>(pointer_y_)};
}

void Office::OnEnter() {
  screen_.DisableSplit();
  screen_.Palette(Viewport::kUpper).Reset();
  screen_.Palette(Viewport::kUpper).Overlay(assets_.Palette(EnginePalette::kOfficeDim), 0);
  phase_ = Phase::kLightsOn;
  phase_seconds_ = 0.0F;
  pointer_x_ = static_cast<float>(kPointerStart.x);
  pointer_y_ = static_cast<float>(kPointerStart.y);
  held_ = {};
  poster_.reset();
  next_poster_.reset();
  keys_.Clear();
  Compose();
}

ComponentType Office::Step(float delta_seconds) {
  auto next = kType;
  auto clicked = false;
  auto quit = false;
  ReadKeys(clicked, quit);
  phase_seconds_ += delta_seconds;
  switch (phase_) {
    case Phase::kLightsOn:
      if (phase_seconds_ >= kLightsSeconds) {
        screen_.Palette(Viewport::kUpper).Overlay(assets_.Palette(EnginePalette::kOffice), 0);
        phase_ = Phase::kPointing;
      }
      break;
    case Phase::kPointing:
      MovePointer(delta_seconds);
      if (clicked) {
        Click();
      }
      break;
    case Phase::kClosing:
    case Phase::kOpening:
      MovePointer(delta_seconds);
      Slide(delta_seconds);
      break;
    case Phase::kLightsOff:
      if (phase_seconds_ >= kLightsSeconds) {
        next = ComponentType::kHighway;
      }
      break;
  }
  Compose();
  return quit ? ComponentType::kQuit : next;
}

void Office::ReadKeys(bool& clicked, bool& quit) {
  while (const auto event = keys_.Next()) {
    const auto pressed = event->action == KeyAction::kPress;
    switch (event->key) {
      case Key::kLeft:
        held_[kLeftArrow] = pressed;
        break;
      case Key::kRight:
        held_[kRightArrow] = pressed;
        break;
      case Key::kUp:
        held_[kUpArrow] = pressed;
        break;
      case Key::kDown:
        held_[kDownArrow] = pressed;
        break;
      case Key::kSpace:
      case Key::kEnter:
        clicked = clicked or pressed;
        break;
      case Key::kEscape:
        quit = quit or pressed;
        break;
      default:
        break;
    }
  }
}

void Office::MovePointer(float delta_seconds) {
  const auto step = kPointerSpeed * delta_seconds;
  const auto horizontal = static_cast<float>(held_[kRightArrow]) - static_cast<float>(held_[kLeftArrow]);
  const auto vertical = static_cast<float>(held_[kDownArrow]) - static_cast<float>(held_[kUpArrow]);
  pointer_x_ = std::clamp(pointer_x_ + (horizontal * step), static_cast<float>(kPointerRange.left),
                          static_cast<float>(kPointerRange.right));
  pointer_y_ = std::clamp(pointer_y_ + (vertical * step), static_cast<float>(kPointerRange.top),
                          static_cast<float>(kPointerRange.bottom));
}

std::optional<std::size_t> Office::PosterFor(std::size_t drawer) const {
  const auto same_drawer = poster_ and *poster_ / 2 == drawer;
  auto poster = std::optional<std::size_t>{same_drawer ? *poster_ ^ 1U : drawer * 2};
  if (not game_.missions_available[*poster]) {
    *poster ^= 1U;
  }
  if (not game_.missions_available[*poster]) {
    poster.reset();
  }
  return poster;
}

void Office::Click() {
  const auto pointer = Pointer();
  if (kDesk.Contains(pointer)) {
    if (poster_ and kPosterAreas[*poster_ / 2].Contains(pointer)) {
      game_.missions_available[*poster_] = false;
      game_.mission = *poster_;
      screen_.Palette(Viewport::kUpper).Overlay(assets_.Palette(EnginePalette::kOfficeDim), 0);
      phase_ = Phase::kLightsOff;
      phase_seconds_ = 0.0F;
    } else {
      // The color of the desk as shown, poster included, picks the drawer.
      ComposeDesk();
      const auto color = screen_.ScreenRow(static_cast<std::uint16_t>(pointer.y))[static_cast<std::size_t>(pointer.x)];
      const auto* const drawer = std::ranges::find(kDrawerColors, color);
      next_poster_ = drawer == kDrawerColors.end()
                         ? std::nullopt
                         : PosterFor(static_cast<std::size_t>(drawer - kDrawerColors.begin()));
      if (poster_) {
        phase_ = Phase::kClosing;
      } else if (next_poster_) {
        poster_ = next_poster_;
        next_poster_.reset();
        poster_y_ = static_cast<float>(kPosterSlides[*poster_].closed_y);
        phase_ = Phase::kOpening;
      }
    }
  }
}

void Office::Slide(float delta_seconds) {
  const auto& slide = kPosterSlides[*poster_];
  if (phase_ == Phase::kOpening) {
    poster_y_ = std::max(poster_y_ - (kSlideSpeed * delta_seconds), static_cast<float>(slide.open_y));
    if (poster_y_ <= static_cast<float>(slide.open_y)) {
      phase_ = Phase::kPointing;
    }
  } else {
    poster_y_ = std::min(poster_y_ + (kSlideSpeed * delta_seconds), static_cast<float>(slide.closed_y));
    if (poster_y_ >= static_cast<float>(slide.closed_y)) {
      poster_ = next_poster_;
      next_poster_.reset();
      if (poster_) {
        poster_y_ = static_cast<float>(kPosterSlides[*poster_].closed_y);
        phase_ = Phase::kOpening;
      } else {
        phase_ = Phase::kPointing;
      }
    }
  }
}

void Office::ComposeDesk() {
  screen_.Blit(assets_.Picture(EnginePicture::kOffice), Point{});
  if (poster_) {
    const auto& bank = assets_.Bank(EngineBank::kOffice);
    const auto& slide = kPosterSlides[*poster_];
    const auto top = static_cast<std::int16_t>(poster_y_);
    // Only the part above the drawer's bottom shows.
    auto poster = bank.GetImage(*poster_);
    const auto visible_rows = std::clamp<int>(slide.clip_bottom - top, 0, poster.height);
    poster.height = static_cast<std::uint16_t>(visible_rows);
    poster.pixels = poster.pixels.first(std::size_t{poster.width} * poster.height);
    screen_.BlitMasked(poster, Point{.x = slide.x, .y = top});
    const auto drawer = *poster_ / 2;
    auto front = bank.GetImage(kFirstDrawerFront + drawer);
    const auto front_rows = std::clamp<int>(slide.clip_bottom - kDrawerFronts[drawer].y, 0, front.height);
    front.height = static_cast<std::uint16_t>(front_rows);
    front.pixels = front.pixels.first(std::size_t{front.width} * front.height);
    screen_.BlitMasked(front, kDrawerFronts[drawer]);
  }
}

void Office::Compose() {
  ComposeDesk();
  const auto& bank = assets_.Bank(EngineBank::kOffice);
  const auto& sprite = bank.sprites[kPointerImage];
  const auto pointer = Pointer();
  screen_.BlitMasked(bank.GetImage(kPointerImage), Point{.x = static_cast<std::int16_t>(pointer.x - sprite.origin_x),
                                                         .y = static_cast<std::int16_t>(pointer.y - sprite.origin_y)});
}

}  // namespace hp2
