#include "core/station.hpp"

#include <gtest/gtest.h>

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
    game_.fuel = 0.25F;
    game_.tires = 1;
  }

  ComponentType Press(Key key) {
    keys_.Record({.key = key, .action = KeyAction::kPress});
    return station_.Step(0.0F);
  }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  GameState game_;
  Station station_{manager_.Engine(), screen_, keys_, game_};
};

TEST_F(StationTest, FillsUpAndRepairsOnce) {
  station_.OnEnter();
  EXPECT_EQ(station_.Cursor(), Station::Item::kExit);
  EXPECT_EQ(Press(Key::kUp), ComponentType::kStation);
  EXPECT_EQ(Press(Key::kUp), ComponentType::kStation);
  EXPECT_EQ(station_.Cursor(), Station::Item::kFillUp);
  EXPECT_EQ(Press(Key::kUp), ComponentType::kStation);  // stays on the first item
  EXPECT_EQ(Press(Key::kSpace), ComponentType::kStation);
  EXPECT_FLOAT_EQ(game_.fuel, 1.0F);
  EXPECT_FALSE(station_.Available(Station::Item::kFillUp));
  EXPECT_EQ(Press(Key::kDown), ComponentType::kStation);
  EXPECT_EQ(Press(Key::kEnter), ComponentType::kStation);
  EXPECT_EQ(game_.tires, GameState::kFullTires);
  EXPECT_FALSE(station_.Available(Station::Item::kRepairTire));
  EXPECT_EQ(Press(Key::kDown), ComponentType::kStation);
  EXPECT_EQ(Press(Key::kSpace), ComponentType::kHighway);
}

TEST_F(StationTest, DrawsTheCursorUnderTheItem) {
  station_.OnEnter();
  for (const auto& line : Station::kCursorLines[2]) {
    EXPECT_EQ(screen_.ScreenRow(static_cast<std::uint16_t>(line.row))[static_cast<std::size_t>(line.left)],
              Station::kCursorColor);
  }
}

TEST_F(StationTest, ARobbedStationOffersNothing) {
  game_.station_robbed = true;
  station_.OnEnter();
  EXPECT_FALSE(station_.Available(Station::Item::kFillUp));
  EXPECT_FALSE(station_.Available(Station::Item::kRepairTire));
  EXPECT_EQ(Press(Key::kUp), ComponentType::kStation);
  EXPECT_EQ(Press(Key::kUp), ComponentType::kStation);
  EXPECT_EQ(Press(Key::kSpace), ComponentType::kStation);
  EXPECT_FLOAT_EQ(game_.fuel, 0.25F);
}

TEST_F(StationTest, FullTiresNeedNoRepair) {
  game_.tires = GameState::kFullTires;
  station_.OnEnter();
  EXPECT_TRUE(station_.Available(Station::Item::kFillUp));
  EXPECT_FALSE(station_.Available(Station::Item::kRepairTire));
}

}  // namespace
}  // namespace hp2
