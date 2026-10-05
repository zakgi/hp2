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

TEST_F(HighwayTest, SSwitchesTheSiren) {
  Press(Key::kS);
  highway_->Step(kFrameSeconds);
  EXPECT_TRUE(highway_->GetMission().GetProgress().siren);
  Press(Key::kS);
  highway_->Step(kFrameSeconds);
  EXPECT_FALSE(highway_->GetMission().GetProgress().siren);
}

TEST_F(HighwayTest, TheEnginesNoteRisesWithTheSpeed) {
  highway_->Step(kFrameSeconds);
  auto& engine = audio_.GetVoice(Highway::kEngineVoice);
  ASSERT_TRUE(engine.Active());
  // Standing: the sample 540 / 640 of its own rate.
  const auto& sample = manager_.Engine().Sound(EngineSound::kEngine);
  const auto own_step = static_cast<float>(sample.rate_hz) / static_cast<float>(AudioEngine::kSampleRate);
  EXPECT_NEAR(engine.Step(), own_step * 540.0F / 640.0F, 1e-4F);
  Record(Key::kUp, KeyAction::kPress);
  RunOneSecond();
  EXPECT_GT(engine.Step(), own_step * 540.0F / 640.0F);
  // Paused, it is silent; it starts again with the game.
  Press(Key::kP);
  highway_->Step(kFrameSeconds);
  EXPECT_FALSE(engine.Active());
  Press(Key::kP);
  highway_->Step(kFrameSeconds);
  EXPECT_TRUE(engine.Active());
  highway_->OnExit();
  EXPECT_FALSE(engine.Active());
}

TEST_F(HighwayTest, TheSirenWailsWhileItIsOn) {
  auto& siren = audio_.GetVoice(Highway::kSirenVoice);
  highway_->Step(kFrameSeconds);
  EXPECT_FALSE(siren.Active());
  Press(Key::kS);
  highway_->Step(kFrameSeconds);
  EXPECT_TRUE(siren.Active());
  // It loops: still sounding long after the sample's length.
  for (auto second = 0; second < 5; ++second) {
    RunOneSecond();
    audio_.Step(1'000'000);
  }
  EXPECT_TRUE(siren.Active());
  Press(Key::kS);
  highway_->Step(kFrameSeconds);
  EXPECT_FALSE(siren.Active());
}

TEST_F(HighwayTest, TheGunSoundsWhenItFires) {
  auto& effect = audio_.GetVoice(Highway::kEffectVoice);
  Record(Key::kSpace, KeyAction::kPress);
  RunOneSecond();
  EXPECT_FALSE(effect.Active());
  Press(Key::kT);
  highway_->Step(kFrameSeconds);
  highway_->Step(kFrameSeconds);
  highway_->Step(kFrameSeconds);
  EXPECT_TRUE(effect.Active());
}

TEST_F(HighwayTest, ComingBackFromTheStationGoesOnWithTheMission) {
  RunOneSecond();
  const auto tick = highway_->GetMission().GetTick();
  game_.fuel = 0.25F;
  game_.tires = 1;
  highway_->OnEnter();
  EXPECT_EQ(highway_->GetMission().GetTick(), tick);
  EXPECT_FLOAT_EQ(highway_->GetMission().GetCondition().fuel, 0.25F);
  EXPECT_EQ(highway_->GetMission().GetCondition().tires, 1);
}

TEST_F(HighwayTest, TheBountyCountsOnFromTheScore) {
  // A second mission, entered with a score from the first.
  Press(Key::kEscape);
  EXPECT_EQ(highway_->Step(kFrameSeconds), ComponentType::kMissionEnd);
  game_.score = 700;
  game_.mission = 2;
  highway_->OnEnter();
  EXPECT_EQ(highway_->GetMission().GetProgress().bounty, 5700);
}

TEST_F(HighwayTest, EscapeAbandonsTheMission) {
  Press(Key::kEscape);
  EXPECT_EQ(highway_->Step(kFrameSeconds), ComponentType::kMissionEnd);
  EXPECT_EQ(game_.end_reason, EndReason::kBountyGone);
}

}  // namespace
}  // namespace hp2
