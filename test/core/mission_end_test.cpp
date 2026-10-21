#include "core/mission_end.hpp"

#include "core/engine_state.hpp"

#include <gtest/gtest.h>

#include <optional>

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
    state_.emplace(manager_.Engine(), screen_, keys_, audio_, 42);
    state_->game.score = 12'345;
    state_->game.mission = 0;
    state_->game.missions_available[0] = false;
  }

  // Runs a fade to its end, then presses Space; returns the component wanted afterward.
  ComponentType FadeThenPress() {
    EXPECT_EQ(end_.Step(*state_, 1.0F), ComponentType::kMissionEnd);
    EXPECT_EQ(end_.Step(*state_, 0.0F), ComponentType::kMissionEnd);
    keys_.Record({.key = Key::kSpace, .action = KeyAction::kPress});
    return end_.Step(*state_, 0.0F);
  }

  // Whether the score palette shows, both its sets.
  [[nodiscard]] bool ShowsTheScorePalette() {
    const auto score = manager_.Engine().Palette(EnginePalette::kScore);
    return std::ranges::equal(screen_.Palette(Viewport::kUpper).Colors().first(score.size()), score);
  }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  AudioEngine audio_;
  std::optional<EngineState> state_;
  MissionEnd end_;
};

TEST_F(MissionEndTest, AnArrestKeepsTheScoreAndTheCareer) {
  state_->mission.GetProgress().end_reason = EndReason::kArrest;
  end_.OnEnter(*state_);
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_FALSE(ShowsTheScorePalette());                             // the arrest picture
  EXPECT_EQ(end_.Step(*state_, 1.0F), ComponentType::kMissionEnd);  // the picture fades out
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);           // the score screen
  EXPECT_TRUE(ShowsTheScorePalette());
  EXPECT_EQ(end_.Step(*state_, 1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(*state_, 0.0F), ComponentType::kOffice);
  EXPECT_EQ(state_->game.score, 12'345U);
  EXPECT_EQ(state_->game.MissionsLeft(), 5U);
  EXPECT_FALSE(state_->mission.GetProgress().end_reason.has_value());
}

TEST_F(MissionEndTest, AFailureStartsANewCareer) {
  state_->mission.GetProgress().end_reason = EndReason::kShot;
  end_.OnEnter(*state_);
  EXPECT_EQ(state_->game.score, 0U);
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_TRUE(ShowsTheScorePalette());  // no picture: the score screen at once
  EXPECT_EQ(end_.Step(*state_, 1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(*state_, 0.0F), ComponentType::kOffice);
  EXPECT_EQ(state_->game.MissionsLeft(), kMissionCount);
}

TEST_F(MissionEndTest, TheLastArrestEndsTheCareer) {
  state_->game.missions_available.fill(false);
  state_->mission.GetProgress().end_reason = EndReason::kArrest;
  end_.OnEnter(*state_);
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(*state_, 1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(FadeThenPress(), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(*state_, 1.0F), ComponentType::kMissionEnd);
  EXPECT_EQ(end_.Step(*state_, 0.0F), ComponentType::kOffice);
  EXPECT_EQ(state_->game.score, 0U);
  EXPECT_EQ(state_->game.MissionsLeft(), kMissionCount);
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
