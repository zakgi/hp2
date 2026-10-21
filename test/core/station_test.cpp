#include "core/station.hpp"

#include "core/engine_state.hpp"

#include <gtest/gtest.h>

#include <optional>

#include "assets.hpp"
#include "core/game_state.hpp"
#include "core/key_events.hpp"
#include "core/screen.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class StationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
    state_.emplace(manager_.Engine(), screen_, keys_, audio_, 42);
    state_->mission.GetCondition().fuel = 0.25F;
    state_->mission.GetCondition().tires = 1;
  }

  ComponentType Press(Key key) {
    keys_.Record({.key = key, .action = KeyAction::Press});
    return station_.Step(*state_, 0.0F);
  }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  AudioEngine audio_;
  std::optional<EngineState> state_;
  Station station_;
};

TEST_F(StationTest, FillsUpAndRepairsOnce) {
  station_.OnEnter(*state_);
  EXPECT_EQ(Station::Cursor(*state_), Station::Item::Exit);
  EXPECT_EQ(Press(Key::Up), ComponentType::Station);
  EXPECT_EQ(Press(Key::Up), ComponentType::Station);
  EXPECT_EQ(Station::Cursor(*state_), Station::Item::FillUp);
  EXPECT_EQ(Press(Key::Up), ComponentType::Station);  // stays on the first item
  EXPECT_EQ(Press(Key::Space), ComponentType::Station);
  EXPECT_FLOAT_EQ(state_->mission.GetCondition().fuel, 1.0F);
  EXPECT_FALSE(Station::Available(*state_, Station::Item::FillUp));
  EXPECT_EQ(Press(Key::Down), ComponentType::Station);
  EXPECT_EQ(Press(Key::Enter), ComponentType::Station);
  EXPECT_EQ(state_->mission.GetCondition().tires, GameState::kFullTires);
  EXPECT_FALSE(Station::Available(*state_, Station::Item::RepairTire));
  EXPECT_EQ(Press(Key::Down), ComponentType::Station);
  EXPECT_EQ(Press(Key::Space), ComponentType::Highway);
}

TEST_F(StationTest, DrawsTheCursorUnderTheItem) {
  station_.OnEnter(*state_);
  for (const auto& line : Station::kCursorLines[2]) {
    EXPECT_EQ(screen_.ScreenRow(static_cast<std::uint16_t>(line.row))[static_cast<std::size_t>(line.left)],
              Station::kCursorColor);
  }
}

TEST_F(StationTest, ARobbedStationOffersNothing) {
  state_->mission.GetProgress().robbed.set(0);
  station_.OnEnter(*state_);
  EXPECT_FALSE(Station::Available(*state_, Station::Item::FillUp));
  EXPECT_FALSE(Station::Available(*state_, Station::Item::RepairTire));
  EXPECT_EQ(Press(Key::Up), ComponentType::Station);
  EXPECT_EQ(Press(Key::Up), ComponentType::Station);
  EXPECT_EQ(Press(Key::Space), ComponentType::Station);
  EXPECT_FLOAT_EQ(state_->mission.GetCondition().fuel, 0.25F);
}

TEST_F(StationTest, FullTiresNeedNoRepair) {
  state_->mission.GetCondition().tires = GameState::kFullTires;
  station_.OnEnter(*state_);
  EXPECT_TRUE(Station::Available(*state_, Station::Item::FillUp));
  EXPECT_FALSE(Station::Available(*state_, Station::Item::RepairTire));
}

}  // namespace
}  // namespace hp2
