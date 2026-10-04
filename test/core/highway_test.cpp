#include "core/highway.hpp"

#include <gtest/gtest.h>

#include <optional>

#include "assets.hpp"
#include "core/audio_engine.hpp"
#include "core/component.hpp"
#include "core/game_state.hpp"
#include "core/key.hpp"
#include "core/key_events.hpp"
#include "core/screen.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

constexpr auto kFrameSeconds = 1.0F / 60.0F;
constexpr auto kFramesPerSecond = 60;

class HighwayTest : public testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
    highway_.emplace(manager_.Engine(), screen_, keys_, audio_, game_, 42);
    highway_->OnEnter();
  }

  void Record(Key key, KeyAction action) { keys_.Record({.key = key, .action = action}); }
  void Press(Key key) {
    Record(key, KeyAction::kPress);
    Record(key, KeyAction::kRelease);
  }
  // Steps one second of frames; returns the component the last frame asked for.
  ComponentType RunOneSecond() {
    auto next = Highway::kType;
    for (auto frame = 0; frame < kFramesPerSecond; ++frame) {
      next = highway_->Step(kFrameSeconds);
    }
    return next;
  }
  [[nodiscard]] WorldPoint GetPlayerPosition() const { return highway_->GetMission().GetPlayer().position; }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  AudioEngine audio_;
  GameState game_;
  std::optional<Highway> highway_;
};

TEST_F(HighwayTest, HoldingUpDrivesForward) {
  const auto start = GetPlayerPosition();
  Record(Key::kUp, KeyAction::kPress);
  EXPECT_EQ(RunOneSecond(), Highway::kType);
  EXPECT_GT(highway_->GetMission().GetPlayer().speed, 0.0F);
  EXPECT_GT(GetLength(GetPlayerPosition() - start), 0.0F);
}

TEST_F(HighwayTest, PauseStopsTheCarInPlace) {
  Press(Key::kP);
  Record(Key::kUp, KeyAction::kPress);
  const auto start = GetPlayerPosition();
  RunOneSecond();
  EXPECT_FLOAT_EQ(GetLength(GetPlayerPosition() - start), 0.0F);
  Press(Key::kP);
  RunOneSecond();
  EXPECT_GT(GetLength(GetPlayerPosition() - start), 0.0F);
}

TEST_F(HighwayTest, TheMapStopsTheCarInPlace) {
  Press(Key::kM);
  Record(Key::kUp, KeyAction::kPress);
  const auto start = GetPlayerPosition();
  RunOneSecond();
  EXPECT_FLOAT_EQ(GetLength(GetPlayerPosition() - start), 0.0F);
  Press(Key::kM);
  RunOneSecond();
  EXPECT_GT(GetLength(GetPlayerPosition() - start), 0.0F);
}

TEST_F(HighwayTest, EscapeAbandonsTheMission) {
  Press(Key::kEscape);
  EXPECT_EQ(highway_->Step(kFrameSeconds), ComponentType::kMissionEnd);
  EXPECT_EQ(game_.end_reason, EndReason::kBountyGone);
}

}  // namespace
}  // namespace hp2
