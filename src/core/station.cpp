#include "core/station.hpp"

#include "core/engine_state.hpp"

#include <cstddef>
#include <utility>

namespace hp2 {

bool Station::IsRobbed(const EngineState& state) {
  // Direct station startup presents the map's first station.
  return state.mission.GetProgress().robbed.test(state.mission.GetStation().value_or(0));
}

Station::Item Station::Cursor(const EngineState& state) {
  const auto& station = std::get<StationState>(state.scene);
  return station.cursor;
}

// Lifecycle hooks use the instance-based ComponentLike protocol.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
void Station::OnEnter(EngineState& state) {
  state.scene.emplace<StationState>();
  state.screen.DisableSplit();
  state.screen.Palette(Viewport::kUpper).Reset();
  state.screen.Palette(Viewport::kUpper).Overlay(state.assets.Palette(EnginePalette::kStation), 0);
  state.keys.Clear();
  Compose(state);
}

bool Station::Available(const EngineState& state, Item item) {
  const auto& station = std::get<StationState>(state.scene);
  auto available = true;
  if (item == Item::kFillUp) {
    available = not IsRobbed(state) and not station.fill_up_used;
  } else if (item == Item::kRepairTire) {
    available =
        not IsRobbed(state) and not station.repair_used and state.mission.GetCondition().tires < GameState::kFullTires;
  }
  return available;
}

void Station::OnExit(EngineState& state) {
  state.scene.emplace<std::monostate>();
}

ComponentType Station::Step(EngineState& state, [[maybe_unused]] float delta_seconds) {
  auto& station = std::get<StationState>(state.scene);
  auto quit = false;
  while (const auto event = state.keys.Next()) {
    if (event->action == KeyAction::kPress) {
      const auto index = std::to_underlying(station.cursor);
      if (event->key == Key::kUp and index > 0) {
        station.cursor = static_cast<Item>(index - 1);
      } else if (event->key == Key::kDown and std::size_t{index} + 1 < kItemCount) {
        station.cursor = static_cast<Item>(index + 1);
      } else if (event->key == Key::kSpace or event->key == Key::kEnter) {
        Choose(state);
      } else if (event->key == Key::kEscape) {
        quit = true;
      }
    }
  }
  Compose(state);
  auto next = kType;
  if (quit) {
    next = ComponentType::kQuit;
  } else if (station.leaving) {
    next = ComponentType::kHighway;
  }
  return next;
}

// NOLINTEND(readability-convert-member-functions-to-static)

void Station::Choose(EngineState& state) {
  auto& station = std::get<StationState>(state.scene);
  if (Available(state, station.cursor)) {
    switch (station.cursor) {
      case Item::kFillUp:
        state.mission.GetCondition().fuel = 1.0F;
        station.fill_up_used = true;
        break;
      case Item::kRepairTire:
        state.mission.GetCondition().tires = GameState::kFullTires;
        station.repair_used = true;
        break;
      case Item::kExit:
        station.leaving = true;
        break;
    }
  }
}

void Station::Compose(EngineState& state) {
  auto& station = std::get<StationState>(state.scene);
  const auto& bank = state.assets.Bank(EngineBank::kStation);
  state.screen.Blit(state.assets.Picture(EnginePicture::kStation), Point{});
  if (IsRobbed(state)) {
    state.screen.Blit(bank.GetImage(kRobbedAttendantImage), kRobbedAttendantPosition);
  } else {
    state.screen.Blit(bank.GetImage(kAttendantImage), kAttendantPosition);
  }
  state.screen.BlitMasked(bank.GetImage(kPanelImage), kPanelPosition);
  const auto item_image = [&state](Item item, std::size_t image, std::size_t grey_image) {
    return Available(state, item) ? image : grey_image;
  };
  state.screen.BlitMasked(bank.GetImage(item_image(Item::kFillUp, kFillUpImage, kFillUpGrayImage)),
                          kItemPositions[std::to_underlying(Item::kFillUp)]);
  state.screen.BlitMasked(bank.GetImage(item_image(Item::kRepairTire, kRepairTireImage, kRepairTireGrayImage)),
                          kItemPositions[std::to_underlying(Item::kRepairTire)]);
  state.screen.BlitMasked(bank.GetImage(kExitImage), kItemPositions[std::to_underlying(Item::kExit)]);
  for (const auto& line : kCursorLines[std::to_underlying(station.cursor)]) {
    state.screen.DrawHorizontalLine(line.left, line.right, line.row, kCursorColor);
  }
}

}  // namespace hp2
