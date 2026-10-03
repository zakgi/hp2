#include "core/station.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/presentation.hpp"

namespace hp2 {

void Station::OnEnter() {
  screen_.DisableSplit();
  screen_.Palette(Viewport::kUpper).Reset();
  ShowPalette(screen_, assets_.Palette(EnginePalette::kStation), kFullBrightness);
  cursor_ = Item::kExit;
  fill_up_used_ = false;
  repair_used_ = false;
  leaving_ = false;
  keys_.Clear();
  Compose();
}

bool Station::Available(Item item) const {
  auto available = true;
  if (item == Item::kFillUp) {
    available = not game_.station_robbed and not fill_up_used_;
  } else if (item == Item::kRepairTyre) {
    available = not game_.station_robbed and not repair_used_ and game_.tyres < GameState::kFullTyres;
  }
  return available;
}

ComponentType Station::Step([[maybe_unused]] float delta_seconds) {
  auto quit = false;
  while (const auto event = keys_.Next()) {
    if (event->action == KeyAction::kPress) {
      const auto index = std::to_underlying(cursor_);
      if (event->key == Key::kUp and index > 0) {
        cursor_ = static_cast<Item>(index - 1);
      } else if (event->key == Key::kDown and std::size_t{index} + 1 < kItemCount) {
        cursor_ = static_cast<Item>(index + 1);
      } else if (event->key == Key::kSpace or event->key == Key::kEnter) {
        Choose();
      } else if (event->key == Key::kEscape) {
        quit = true;
      }
    }
  }
  Compose();
  auto next = kType;
  if (quit) {
    next = ComponentType::kQuit;
  } else if (leaving_) {
    next = ComponentType::kDriving;
  }
  return next;
}

void Station::Choose() {
  if (Available(cursor_)) {
    switch (cursor_) {
      case Item::kFillUp:
        game_.fuel = 1.0F;
        fill_up_used_ = true;
        break;
      case Item::kRepairTyre:
        game_.tyres = GameState::kFullTyres;
        repair_used_ = true;
        break;
      case Item::kExit:
        leaving_ = true;
        break;
    }
  }
}

void Station::Compose() {
  const auto& bank = assets_.Bank(EngineBank::kStation);
  screen_.Blit(assets_.Picture(EnginePicture::kStation), Point{});
  if (game_.station_robbed) {
    screen_.Blit(bank.GetImage(kRobbedAttendantImage), kRobbedAttendantAt);
  } else {
    screen_.Blit(bank.GetImage(kAttendantImage), kAttendantAt);
  }
  screen_.BlitMasked(bank.GetImage(kPanelImage), kPanelAt);
  const auto item_image = [this](Item item, std::size_t image, std::size_t grey_image) {
    return Available(item) ? image : grey_image;
  };
  screen_.BlitMasked(bank.GetImage(item_image(Item::kFillUp, kFillUpImage, kFillUpGreyImage)),
                     kItemsAt[std::to_underlying(Item::kFillUp)]);
  screen_.BlitMasked(bank.GetImage(item_image(Item::kRepairTyre, kRepairTyreImage, kRepairTyreGreyImage)),
                     kItemsAt[std::to_underlying(Item::kRepairTyre)]);
  screen_.BlitMasked(bank.GetImage(kExitImage), kItemsAt[std::to_underlying(Item::kExit)]);
  for (const auto& line : kCursorLines[std::to_underlying(cursor_)]) {
    screen_.DrawHorizontalLine(line.left, line.right, line.row, kCursorColor);
  }
}

}  // namespace hp2
