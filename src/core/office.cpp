#include "core/office.hpp"

#include "core/engine_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hp2 {

namespace {

enum Arrow : std::uint8_t { kLeftArrow, kRightArrow, kUpArrow, kDownArrow };

}  // namespace

std::optional<std::size_t> Office::Poster(const EngineState& state) {
  const auto& office = std::get<OfficeState>(state.scene);
  return office.poster;
}

Point Office::Pointer(const EngineState& state) {
  const auto& office = std::get<OfficeState>(state.scene);
  return Point{.x = static_cast<std::int16_t>(office.pointer_x), .y = static_cast<std::int16_t>(office.pointer_y)};
}

// Lifecycle hooks use the instance-based ComponentLike protocol.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
void Office::OnEnter(EngineState& state) {
  state.scene.emplace<OfficeState>();
  auto& office = std::get<OfficeState>(state.scene);
  state.screen.DisableSplit();
  state.screen.Palette(Viewport::Upper).Reset();
  state.screen.Palette(Viewport::Upper).Overlay(state.assets.Palette(EnginePalette::OfficeDim), 0);
  office.pointer_x = static_cast<float>(kPointerStart.x);
  office.pointer_y = static_cast<float>(kPointerStart.y);
  state.keys.Clear();
  Compose(state);
}

void Office::OnExit(EngineState& state) {
  state.scene.emplace<std::monostate>();
}

ComponentType Office::Step(EngineState& state, float delta_seconds) {
  auto& office = std::get<OfficeState>(state.scene);
  auto next = kType;
  auto clicked = false;
  auto quit = false;
  ReadKeys(state, clicked, quit);
  office.phase_seconds += delta_seconds;
  switch (office.phase) {
    case OfficePhase::LightsOn:
      if (office.phase_seconds >= kLightsSeconds) {
        state.screen.Palette(Viewport::Upper).Overlay(state.assets.Palette(EnginePalette::Office), 0);
        office.phase = OfficePhase::Pointing;
      }
      break;
    case OfficePhase::Pointing:
      MovePointer(state, delta_seconds);
      if (clicked) {
        Click(state);
      }
      break;
    case OfficePhase::Closing:
    case OfficePhase::Opening:
      MovePointer(state, delta_seconds);
      Slide(state, delta_seconds);
      break;
    case OfficePhase::LightsOff:
      if (office.phase_seconds >= kLightsSeconds) {
        next = ComponentType::Highway;
      }
      break;
  }
  Compose(state);
  return quit ? ComponentType::Quit : next;
}

// NOLINTEND(readability-convert-member-functions-to-static)

void Office::ReadKeys(EngineState& state, bool& clicked, bool& quit) {
  auto& office = std::get<OfficeState>(state.scene);
  while (const auto event = state.keys.Next()) {
    const auto pressed = event->action == KeyAction::Press;
    switch (event->key) {
      case Key::Left:
        office.held[kLeftArrow] = pressed;
        break;
      case Key::Right:
        office.held[kRightArrow] = pressed;
        break;
      case Key::Up:
        office.held[kUpArrow] = pressed;
        break;
      case Key::Down:
        office.held[kDownArrow] = pressed;
        break;
      case Key::Space:
      case Key::Enter:
        clicked = clicked or pressed;
        break;
      case Key::Escape:
        quit = quit or pressed;
        break;
      default:
        break;
    }
  }
}

void Office::MovePointer(EngineState& state, float delta_seconds) {
  auto& office = std::get<OfficeState>(state.scene);
  const auto step = kPointerSpeed * delta_seconds;
  const auto horizontal = static_cast<float>(office.held[kRightArrow]) - static_cast<float>(office.held[kLeftArrow]);
  const auto vertical = static_cast<float>(office.held[kDownArrow]) - static_cast<float>(office.held[kUpArrow]);
  office.pointer_x = std::clamp(office.pointer_x + (horizontal * step), static_cast<float>(kPointerRange.left),
                                static_cast<float>(kPointerRange.right));
  office.pointer_y = std::clamp(office.pointer_y + (vertical * step), static_cast<float>(kPointerRange.top),
                                static_cast<float>(kPointerRange.bottom));
}

std::optional<std::size_t> Office::PosterFor(const EngineState& state, std::size_t drawer) {
  const auto& office = std::get<OfficeState>(state.scene);
  const auto same_drawer = office.poster and *office.poster / 2 == drawer;
  auto poster = std::optional<std::size_t>{same_drawer ? *office.poster ^ 1U : drawer * 2};
  if (not state.game.missions_available[*poster]) {
    *poster ^= 1U;
  }
  if (not state.game.missions_available[*poster]) {
    poster.reset();
  }
  return poster;
}

void Office::Click(EngineState& state) {
  auto& office = std::get<OfficeState>(state.scene);
  const auto pointer = Pointer(state);
  if (kDesk.Contains(pointer)) {
    if (office.poster and kPosterAreas[*office.poster / 2].Contains(pointer)) {
      state.game.missions_available[*office.poster] = false;
      state.game.mission = office.poster;
      state.screen.Palette(Viewport::Upper).Overlay(state.assets.Palette(EnginePalette::OfficeDim), 0);
      office.phase = OfficePhase::LightsOff;
      office.phase_seconds = 0.0F;
    } else {
      // The color of the desk as shown, poster included, picks the drawer.
      ComposeDesk(state);
      const auto color =
          state.screen.ScreenRow(static_cast<std::uint16_t>(pointer.y))[static_cast<std::size_t>(pointer.x)];
      const auto* const drawer = std::ranges::find(kDrawerColors, color);
      office.next_poster = drawer == kDrawerColors.end()
                               ? std::nullopt
                               : PosterFor(state, static_cast<std::size_t>(drawer - kDrawerColors.begin()));
      if (office.poster) {
        office.phase = OfficePhase::Closing;
      } else if (office.next_poster) {
        office.poster = office.next_poster;
        office.next_poster.reset();
        office.poster_y = static_cast<float>(kPosterSlides[*office.poster].closed_y);
        office.phase = OfficePhase::Opening;
      }
    }
  }
}

void Office::Slide(EngineState& state, float delta_seconds) {
  auto& office = std::get<OfficeState>(state.scene);
  const auto& slide = kPosterSlides[*office.poster];
  if (office.phase == OfficePhase::Opening) {
    office.poster_y = std::max(office.poster_y - (kSlideSpeed * delta_seconds), static_cast<float>(slide.open_y));
    if (office.poster_y <= static_cast<float>(slide.open_y)) {
      office.phase = OfficePhase::Pointing;
    }
  } else {
    office.poster_y = std::min(office.poster_y + (kSlideSpeed * delta_seconds), static_cast<float>(slide.closed_y));
    if (office.poster_y >= static_cast<float>(slide.closed_y)) {
      office.poster = office.next_poster;
      office.next_poster.reset();
      if (office.poster) {
        office.poster_y = static_cast<float>(kPosterSlides[*office.poster].closed_y);
        office.phase = OfficePhase::Opening;
      } else {
        office.phase = OfficePhase::Pointing;
      }
    }
  }
}

void Office::ComposeDesk(EngineState& state) {
  auto& office = std::get<OfficeState>(state.scene);
  state.screen.Blit(state.assets.Picture(EnginePicture::Office), Point{});
  if (office.poster) {
    const auto& bank = state.assets.Bank(EngineBank::Office);
    const auto& slide = kPosterSlides[*office.poster];
    const auto top = static_cast<std::int16_t>(office.poster_y);
    // Only the part above the drawer's bottom shows.
    auto poster = bank.GetImage(*office.poster);
    const auto visible_rows = std::clamp<int>(slide.clip_bottom - top, 0, poster.height);
    poster.height = static_cast<std::uint16_t>(visible_rows);
    poster.pixels = poster.pixels.first(std::size_t{poster.width} * poster.height);
    state.screen.BlitMasked(poster, Point{.x = slide.x, .y = top});
    const auto drawer = *office.poster / 2;
    auto front = bank.GetImage(kFirstDrawerFront + drawer);
    const auto front_rows = std::clamp<int>(slide.clip_bottom - kDrawerFronts[drawer].y, 0, front.height);
    front.height = static_cast<std::uint16_t>(front_rows);
    front.pixels = front.pixels.first(std::size_t{front.width} * front.height);
    state.screen.BlitMasked(front, kDrawerFronts[drawer]);
  }
}

void Office::Compose(EngineState& state) {
  ComposeDesk(state);
  const auto& bank = state.assets.Bank(EngineBank::Office);
  const auto& sprite = bank.sprites[kPointerImage];
  const auto pointer = Pointer(state);
  state.screen.BlitMasked(bank.GetImage(kPointerImage),
                          Point{.x = static_cast<std::int16_t>(pointer.x - sprite.origin_x),
                                .y = static_cast<std::int16_t>(pointer.y - sprite.origin_y)});
}

}  // namespace hp2
