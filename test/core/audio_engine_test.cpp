// The audio engine: its clock, the voices' sides, headroom and the full ring.

#include "core/audio_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "assets.hpp"
#include "core/voice.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class AudioEngineTest : public testing::Test {
 protected:
  static constexpr std::size_t kSettle = ResamplerKernel::kTaps;  // frames whose window touches the sample start

  std::vector<AudioFrame> Drain() {
    auto frames = std::vector<AudioFrame>(engine_.Output().Count());
    engine_.Output().Pop(frames);
    return frames;
  }

  void StartFlat(std::size_t index, std::int8_t level) {
    auto& voice = engine_.GetVoice(index);
    voice.SetStep(1.0F);
    voice.SetVolume(1.0F);
    voice.Start(flat_[index], 0, 0);
    std::ranges::fill(flat_[index], level);
  }

  static constexpr std::size_t kFlatLength = 512;

  AudioEngine engine_{};
  std::array<std::array<std::int8_t, kFlatLength>, AudioEngine::kVoiceCount> flat_{};
};

TEST_F(AudioEngineTest, ClockProducesTheExactFrameCount) {
  constexpr std::uint32_t kStepMicroseconds = 20;  // 0.96 frames at 48 kHz
  for (std::size_t step = 0; step < 25; ++step) {
    engine_.Step(kStepMicroseconds);
  }
  EXPECT_EQ(engine_.Output().Count(), 24U);
  engine_.Step(999);
  engine_.Step(1);
  EXPECT_EQ(engine_.Output().Count(), 24U + 48U);
}

TEST_F(AudioEngineTest, SilenceWhenNothingPlays) {
  engine_.Step(1000);
  for (const auto frame : Drain()) {
    EXPECT_EQ(frame.left, 0);
    EXPECT_EQ(frame.right, 0);
  }
}

TEST_F(AudioEngineTest, VoicesFollowPaulaLanes) {
  StartFlat(0, 100);
  StartFlat(1, 50);
  engine_.Step(2000);
  const auto frames = Drain();
  ASSERT_EQ(frames.size(), 96U);
  for (std::size_t index = kSettle; index < frames.size(); ++index) {
    EXPECT_NEAR(frames[index].left, 100 * 128, 1) << "at " << index;
    EXPECT_NEAR(frames[index].right, 50 * 128, 1) << "at " << index;
  }
}

TEST_F(AudioEngineTest, TwoFullVoicesPerSideDoNotClip) {
  StartFlat(0, 127);
  StartFlat(3, 127);
  StartFlat(1, -128);
  StartFlat(2, -128);
  engine_.Step(2000);
  const auto frames = Drain();
  for (std::size_t index = kSettle; index < frames.size(); ++index) {
    EXPECT_NEAR(frames[index].left, 2 * 127 * 128, 1) << "at " << index;
    EXPECT_NEAR(frames[index].right, -32768, 1) << "at " << index;
  }
}

TEST_F(AudioEngineTest, FullRingDropsAndCounts) {
  engine_.Step(1'000'000);
  EXPECT_EQ(engine_.Output().Count(), AudioEngine::Ring::Capacity());
  EXPECT_EQ(engine_.TakeDroppedFrames(), AudioEngine::kSampleRate - AudioEngine::Ring::Capacity());
  EXPECT_EQ(engine_.TakeDroppedFrames(), 0U);
}

TEST_F(AudioEngineTest, DroppedFramesStillAdvanceTheVoices) {
  StartFlat(0, 100);
  engine_.Step(1'000'000);
  EXPECT_FALSE(engine_.GetVoice(0).Active());
}

TEST(AudioEngineMusic, PlaysTheTitleMusicOnBothSides) {
  if (not test::Executable()) {
    GTEST_SKIP() << "game files not present in " << test::DiskImage();
  }
  auto manager = host::AssetManager{};
  ASSERT_TRUE(manager.Load(test::DiskImage()));
  auto engine = AudioEngine{};
  engine.PlayMusic(manager.Engine().title_music);
  auto peak_left = 0;
  auto peak_right = 0;
  auto frames = std::vector<AudioFrame>(AudioEngine::Ring::Capacity());
  for (auto step = 0; step < 100; ++step) {  // 3 seconds
    engine.Step(30'000);
    for (const auto frame : engine.Output().Pop(frames)) {
      peak_left = std::max(peak_left, std::abs(int{frame.left}));
      peak_right = std::max(peak_right, std::abs(int{frame.right}));
    }
  }
  EXPECT_EQ(engine.TakeDroppedFrames(), 0U);
  EXPECT_GT(peak_left, 1'000);
  EXPECT_GT(peak_right, 1'000);
}

}  // namespace
}  // namespace hp2
