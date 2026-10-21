#include "core/highway.hpp"

#include "core/engine_state.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
    state_.emplace(manager_.Engine(), screen_, keys_, audio_, 42);
    highway_.OnEnter(*state_);
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
      next = highway_.Step(*state_, kFrameSeconds);
    }
    return next;
  }
  [[nodiscard]] WorldPoint GetPlayerPosition() const { return state_->mission.GetPlayer().position; }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  AudioEngine audio_;
  std::optional<EngineState> state_;
  Highway highway_;
};

TEST_F(HighwayTest, HoldingUpDrivesForward) {
  const auto start = GetPlayerPosition();
  Record(Key::kUp, KeyAction::kPress);
  EXPECT_EQ(RunOneSecond(), Highway::kType);
  EXPECT_GT(state_->mission.GetPlayer().speed, 0.0F);
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
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_TRUE(state_->mission.GetProgress().siren);
  Press(Key::kS);
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_FALSE(state_->mission.GetProgress().siren);
}

TEST_F(HighwayTest, TheEnginesNoteRisesWithTheSpeed) {
  highway_.Step(*state_, kFrameSeconds);
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
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_FALSE(engine.Active());
  Press(Key::kP);
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_TRUE(engine.Active());
  highway_.OnExit(*state_);
  EXPECT_FALSE(engine.Active());
}

TEST_F(HighwayTest, TheSirenWailsWhileItIsOn) {
  auto& siren = audio_.GetVoice(Highway::kSirenVoice);
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_FALSE(siren.Active());
  Press(Key::kS);
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_TRUE(siren.Active());
  // It loops: still sounding long after the sample's length.
  for (auto second = 0; second < 5; ++second) {
    RunOneSecond();
    audio_.Step(1'000'000);
  }
  EXPECT_TRUE(siren.Active());
  Press(Key::kS);
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_FALSE(siren.Active());
}

TEST_F(HighwayTest, TheGunSoundsWhenItFires) {
  auto& effect = audio_.GetVoice(Highway::kEffectVoice);
  Record(Key::kSpace, KeyAction::kPress);
  RunOneSecond();
  EXPECT_FALSE(effect.Active());
  Press(Key::kT);
  highway_.Step(*state_, kFrameSeconds);
  highway_.Step(*state_, kFrameSeconds);
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_TRUE(effect.Active());
}

TEST_F(HighwayTest, ComingBackFromTheStationGoesOnWithTheMission) {
  RunOneSecond();
  const auto tick = state_->mission.GetTick();
  state_->mission.GetCondition().fuel = 0.25F;
  state_->mission.GetCondition().tires = 1;
  highway_.OnEnter(*state_);
  EXPECT_EQ(state_->mission.GetTick(), tick);
  EXPECT_FLOAT_EQ(state_->mission.GetCondition().fuel, 0.25F);
  EXPECT_EQ(state_->mission.GetCondition().tires, 1);
}

TEST_F(HighwayTest, TheBountyCountsOnFromTheScore) {
  // A second mission, entered with a score from the first.
  Press(Key::kEscape);
  EXPECT_EQ(highway_.Step(*state_, kFrameSeconds), ComponentType::kMissionEnd);
  state_->game.score = 700;
  state_->game.mission = 2;
  highway_.OnEnter(*state_);
  EXPECT_EQ(state_->mission.GetProgress().bounty, 5700);
}

TEST_F(HighwayTest, EscapeAbandonsTheMission) {
  Press(Key::kEscape);
  EXPECT_EQ(highway_.Step(*state_, kFrameSeconds), ComponentType::kMissionEnd);
  EXPECT_EQ(state_->mission.GetProgress().end_reason, EndReason::kBountyGone);
}

TEST_F(HighwayTest, ReplacingTheControllerKeepsTheMissionAndPauseState) {
  RunOneSecond();
  const auto tick = state_->mission.GetTick();
  auto replacement = Highway{};
  EXPECT_EQ(replacement.Step(*state_, kFrameSeconds), Highway::kType);
  EXPECT_EQ(state_->mission.GetTick(), tick + 1);
  Press(Key::kP);
  replacement.Step(*state_, kFrameSeconds);
  highway_.Step(*state_, kFrameSeconds);
  EXPECT_EQ(state_->mission.GetTick(), tick + 1);
}

TEST_F(HighwayTest, StationServicesUpdateTheMissionAcrossEngineTransitions) {
  auto engine = Engine<Highway, Station>{*state_};
  ASSERT_TRUE(engine.Start(ComponentType::kHighway));
  RunOneSecond();
  const auto tick = state_->mission.GetTick();
  state_->mission.GetCondition().fuel = 0.25F;
  state_->mission.GetCondition().tires = 1;
  ASSERT_TRUE(engine.Switch(ComponentType::kStation));
  EXPECT_TRUE(std::holds_alternative<StationState>(state_->scene));
  Press(Key::kUp);
  Press(Key::kUp);
  Press(Key::kSpace);
  ASSERT_TRUE(engine.Step(0.0F));
  EXPECT_FLOAT_EQ(state_->mission.GetCondition().fuel, 1.0F);
  Press(Key::kDown);
  Press(Key::kSpace);
  ASSERT_TRUE(engine.Step(0.0F));
  EXPECT_EQ(state_->mission.GetCondition().tires, GameState::kFullTires);
  Press(Key::kDown);
  Press(Key::kSpace);
  ASSERT_TRUE(engine.Step(0.0F));
  EXPECT_EQ(engine.Active(), ComponentType::kHighway);
  EXPECT_TRUE(std::holds_alternative<HighwayState>(state_->scene));
  EXPECT_EQ(state_->mission.GetTick(), tick);
  EXPECT_FLOAT_EQ(state_->mission.GetCondition().fuel, 1.0F);
  EXPECT_EQ(state_->mission.GetCondition().tires, GameState::kFullTires);
}

TEST_F(HighwayTest, AStationVisitPreservesTheNextMissionsSeed) {
  auto reference = EngineState{manager_.Engine(), screen_, keys_, audio_, 42};
  auto driver = Highway{};
  driver.OnEnter(reference);

  auto engine = Engine<Highway, Station>{*state_};
  ASSERT_TRUE(engine.Start(ComponentType::kHighway));
  ASSERT_TRUE(engine.Switch(ComponentType::kStation));
  ASSERT_TRUE(engine.Switch(ComponentType::kHighway));
  Press(Key::kEscape);
  ASSERT_EQ(highway_.Step(*state_, 0.0F), ComponentType::kMissionEnd);
  highway_.OnEnter(*state_);

  Press(Key::kEscape);
  ASSERT_EQ(driver.Step(reference, 0.0F), ComponentType::kMissionEnd);
  driver.OnEnter(reference);
  EXPECT_FLOAT_EQ(GetLength(state_->mission.GetPlayer().position - reference.mission.GetPlayer().position), 0.0F);
  EXPECT_FLOAT_EQ(GetLength(state_->mission.GetTarget().position - reference.mission.GetTarget().position), 0.0F);
  EXPECT_EQ(state_->seeds.Next(), reference.seeds.Next());
}

TEST_F(HighwayTest, ANewMissionStartsWithFreshViewHistory) {
  Press(Key::kT);
  Record(Key::kUp, KeyAction::kPress);
  RunOneSecond();
  Press(Key::kEscape);
  ASSERT_EQ(highway_.Step(*state_, kFrameSeconds), ComponentType::kMissionEnd);
  state_->game.mission = 2;
  highway_.OnEnter(*state_);
  auto expected = Screen{};
  auto view = DriverView{manager_.Engine(), expected, state_->mission.GetRoad()};
  view.Reset(state_->mission);
  view.Draw(state_->mission);
  EXPECT_EQ(state_->mission.GetTick(), 0U);
  for (const auto viewport : {Viewport::kUpper, Viewport::kLower}) {
    EXPECT_TRUE(std::ranges::equal(screen_.Pixels(viewport), expected.Pixels(viewport)));
    EXPECT_TRUE(std::ranges::equal(screen_.Palette(viewport).Colors(), expected.Palette(viewport).Colors()));
  }
}

}  // namespace
}  // namespace hp2
