#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/component.hpp"
#include "core/engine_assets.hpp"
#include "core/game_state.hpp"
#include "core/key_events.hpp"
#include "core/screen.hpp"

namespace hp2 {

// The gas station, when the car stops in a station's driveway (StationScene, 0:349c): the
// attendant, or the attendant tied up when the station has been robbed, and a menu of FILL UP,
// REPAIR TYRE and EXIT. Both services are free, each can be used once, and neither works at a
// robbed station; repair also needs a tire to repair. Up and Down move the cursor, Space or Enter
// choose; Escape quits.
class Station {
 public:
  static constexpr ComponentType kType = ComponentType::kStation;

  enum class Item : std::uint8_t { kFillUp, kRepairTire, kExit };
  static constexpr std::size_t kItemCount = 3;

  // A horizontal line of the cursor: columns left to right on one row.
  struct CursorLine {
    std::int16_t left;
    std::int16_t right;
    std::int16_t row;
  };

  // STATION.IMG, 0-based: the attendant tied up, the menu panel, the items, the greyed items, the
  // attendant.
  static constexpr std::size_t kRobbedAttendantImage = 0;
  static constexpr std::size_t kPanelImage = 1;
  static constexpr std::size_t kFillUpImage = 2;
  static constexpr std::size_t kRepairTireImage = 3;
  static constexpr std::size_t kExitImage = 4;
  static constexpr std::size_t kFillUpGrayImage = 5;
  static constexpr std::size_t kRepairTireGrayImage = 6;
  static constexpr std::size_t kAttendantImage = 7;
  static constexpr Point kRobbedAttendantPosition{.x = 256, .y = 96};
  static constexpr Point kAttendantPosition{.x = 64, .y = 64};
  static constexpr Point kPanelPosition{.x = 160, .y = 4};
  static constexpr auto kItemPositions =
      std::to_array<Point>({{.x = 168, .y = 13}, {.x = 168, .y = 26}, {.x = 168, .y = 48}});
  // Two lines in color 11 frame the item under the cursor (stationCursorLines, 0:39fe).
  static constexpr std::uint8_t kCursorColor = 11;
  static constexpr auto kCursorLines = std::to_array<std::array<CursorLine, 2>>({
      {{{.left = 168, .right = 285, .row = 11}, {.left = 168, .right = 285, .row = 21}}},
      {{{.left = 168, .right = 285, .row = 24}, {.left = 168, .right = 285, .row = 34}}},
      {{{.left = 168, .right = 213, .row = 46}, {.left = 168, .right = 213, .row = 56}}},
  });

  Station(const EngineAssets& assets, Screen& screen, KeyEvents& keys, GameState& game)
      : assets_(assets), screen_(screen), keys_(keys), game_(game) {}

  void OnEnter();
  void OnExit() {}
  ComponentType Step(float delta_seconds);

  // For tests: the item under the cursor, and whether an item still works.
  [[nodiscard]] Item Cursor() const { return cursor_; }
  [[nodiscard]] bool Available(Item item) const;

 private:
  void Choose();
  void Compose();

  const EngineAssets& assets_;
  Screen& screen_;
  KeyEvents& keys_;
  GameState& game_;
  Item cursor_{Item::kExit};
  bool fill_up_used_{};
  bool repair_used_{};
  bool leaving_{};
};

static_assert(ComponentLike<Station>);

}  // namespace hp2
