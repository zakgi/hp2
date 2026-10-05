#include "core/mission_end.hpp"

#include <gtest/gtest.h>

#include <algorithm>

#include "assets.hpp"
#include "core/game_state.hpp"
#include "core/key_events.hpp"
#include "core/screen.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class MissionEndTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
    game_.score = 12'345;
    game_.mission = 0;
    game_.missions_available[0] = false;
  }

  // Runs a fade to its end, then presses Space; returns the component wanted afterward.
  ComponentType FadeThenPress() {
    EXPECT_EQ(end_.Step(1.0F), ComponentType::kMissionEnd);
    EXPECT_EQ(end_.Step(0.0F), ComponentType::kMissionEnd);
    keys_.Record({.key = Key::kSpace, .action = KeyAction::kPress});
    return end_.Step(0.0F);
  }

  // Whether the score palette shows, both its sets.
  [[nodiscard]] bool ShowsTheScorePalette() {
    const auto score = manager_.Engine().Palette(EnginePalette::kScore);
    return std::ranges::equal(screen_.Palette(Viewport::kUpper).Colors().first(score.size()), score);
  }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  GameState game_;
  MissionEnd end_{manager_.Engine(), screen_, keys_, game_};
};

TEST_F(MissionEndTest, AnArrestKeepsTheScoreAndTheCareer) {
  game_.end_reason = EndReason::kArrest;
  end_.OnEnter();
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_FALSE(ShowsTheScorePalette());                    // the arrest picture
  EXPECT_EQ(end_.Step(1.0F), ComponentType::kMissionEnd);  // the picture fades out
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);  // the score screen
  EXPECT_TRUE(ShowsTheScorePalette());
  EXPECT_EQ(end_.Step(1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(0.0F), ComponentType::kOffice);
  EXPECT_EQ(game_.score, 12'345U);
  EXPECT_EQ(game_.MissionsLeft(), 5U);
  EXPECT_FALSE(game_.end_reason.has_value());
}

TEST_F(MissionEndTest, AFailureStartsANewCareer) {
  game_.end_reason = EndReason::kShot;
  end_.OnEnter();
  EXPECT_EQ(game_.score, 0U);
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_TRUE(ShowsTheScorePalette());  // no picture: the score screen at once
  EXPECT_EQ(end_.Step(1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(0.0F), ComponentType::kOffice);
  EXPECT_EQ(game_.MissionsLeft(), kMissionCount);
}

TEST_F(MissionEndTest, TheLastArrestEndsTheCareer) {
  game_.missions_available.fill(false);
  game_.end_reason = EndReason::kArrest;
  end_.OnEnter();
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(0.0F), ComponentType::kOffice);
  EXPECT_EQ(game_.score, 0U);
  EXPECT_EQ(game_.MissionsLeft(), kMissionCount);
}

TEST_F(MissionEndTest, EveryEndingHasItsPictureOrText) {
  EXPECT_EQ(MissionEnd::GetEnding(EndReason::kOutOfFuel).picture, EnginePicture::kOutOfFuel);
  EXPECT_EQ(MissionEnd::GetEnding(EndReason::kTiresGone).picture, EnginePicture::kTiresGone);
  EXPECT_FALSE(MissionEnd::GetEnding(EndReason::kStationsRobbed).text.empty());
  EXPECT_FALSE(MissionEnd::GetEnding(EndReason::kArrest).game_over);
  EXPECT_TRUE(MissionEnd::GetEnding(EndReason::kBountyGone).game_over);
}

}  // namespace
}  // namespace hp2
