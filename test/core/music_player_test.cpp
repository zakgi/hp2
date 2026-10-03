#include "core/music_player.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/music_module.hpp"
#include "core/voice.hpp"

namespace hp2 {
namespace {

constexpr auto kSampleRate = std::uint32_t{48'000};
constexpr auto kTolerance = 1e-5F;

float ExpectedStep(float period) {
  return static_cast<float>(kMusicSoundClockHz) / (period * static_cast<float>(kSampleRate));
}

// Two patterns played in the order 0, 1. Instrument 1 is a one-shot, instrument 2 a loop after
// a head; only channel 0 is written unless a test writes more.
class MusicPlayerTest : public ::testing::Test {
 protected:
  static constexpr std::uint16_t kTimer = 14'188;  // about 50 ticks a second

  MusicPlayerTest() {
    // Instrument 1's 64 bytes, then instrument 2's 100.
    samples_[0] = ModuleSample{.offset = 0, .size = 64, .length = 64, .loop_start = 0, .loop_length = 2, .volume = 64};
    samples_[1] =
        ModuleSample{.offset = 64, .size = 100, .length = 40, .loop_start = 40, .loop_length = 60, .volume = 32};
    std::fill_n(sample_data_.begin(), 64, std::int8_t{100});
    std::fill_n(sample_data_.begin() + 64, 100, std::int8_t{50});
  }

  void Write(std::size_t pattern, std::size_t row, std::size_t channel, ModuleNote note) {
    notes_[(pattern * MusicModule::kPatternNotes) + (row * MusicModule::kChannelCount) + channel] = note;
  }

  void Play() {
    player_.Play(MusicModule{
        .sample_data = sample_data_, .samples = samples_, .positions = positions_, .notes = notes_, .timer = kTimer});
  }

  void Ticks(std::size_t count) {
    for (auto tick = std::size_t{0}; tick < count; ++tick) {
      player_.Tick();
    }
  }

  std::array<std::int8_t, 164> sample_data_{};
  std::array<ModuleSample, MusicModule::kSampleCount> samples_{};
  std::array<std::uint8_t, 2> positions_{0, 1};
  std::vector<ModuleNote> notes_ = std::vector<ModuleNote>(2 * MusicModule::kPatternNotes);
  std::array<Voice, MusicPlayer::kChannelCount> voices_{};
  MusicPlayer player_{voices_, kSampleRate};
};

TEST_F(MusicPlayerTest, PlaysTheFirstRowAtOnce) {
  Write(0, 0, 0, {.period = 428, .sample = 1});
  Play();
  EXPECT_TRUE(player_.Playing());
  EXPECT_EQ(player_.Row(), 1U);
  EXPECT_TRUE(voices_[0].Active());
  EXPECT_NEAR(voices_[0].Step(), ExpectedStep(428.0F), kTolerance);
  EXPECT_FLOAT_EQ(voices_[0].Volume(), 1.0F);
  EXPECT_FALSE(voices_[1].Active());
}

TEST_F(MusicPlayerTest, PlaysARowEverySixTicksAndRepeatsTheSong) {
  Play();
  Ticks(5);
  EXPECT_EQ(player_.Row(), 1U);
  Ticks(1);
  EXPECT_EQ(player_.Row(), 2U);
  Ticks(MusicPlayer::kTicksPerRow * (MusicModule::kRowCount - 2));
  EXPECT_EQ(player_.Position(), 1U);
  EXPECT_EQ(player_.Row(), 0U);
  Ticks(MusicPlayer::kTicksPerRow * MusicModule::kRowCount);
  EXPECT_EQ(player_.Position(), 0U);
}

TEST_F(MusicPlayerTest, TicksAtTheModulesTempo) {
  Play();
  // A row is 6 ticks of kTimer counts of the timer clock.
  const auto row_samples = 6.0 * kTimer * kSampleRate / kMusicTimerClockHz;
  auto samples = std::size_t{0};
  while (player_.Row() == 1) {
    player_.AdvanceSample();
    ++samples;
  }
  EXPECT_NEAR(static_cast<double>(samples), row_samples, 1.0);
}

TEST_F(MusicPlayerTest, OneShotsEndAndLoopsKeepPlaying) {
  Write(0, 0, 0, {.period = 214, .sample = 1});
  Write(0, 0, 1, {.period = 214, .sample = 2});
  Play();
  for (auto sample = 0; sample < 2000; ++sample) {
    voices_[0].Render();
    voices_[1].Render();
  }
  EXPECT_FALSE(voices_[0].Active());
  EXPECT_TRUE(voices_[1].Active());
  EXPECT_FLOAT_EQ(voices_[1].Volume(), 0.5F);
}

TEST_F(MusicPlayerTest, VolumeEffectsAdjustTheInstrumentsVolume) {
  Write(0, 0, 0, {.period = 428, .sample = 2, .effect = 5, .parameter = 40});
  Write(0, 0, 1, {.period = 428, .sample = 2, .effect = 6, .parameter = 8});
  Write(0, 0, 2, {.period = 428, .sample = 2, .effect = 6, .parameter = 40});
  Play();
  EXPECT_FLOAT_EQ(voices_[0].Volume(), 1.0F);
  EXPECT_FLOAT_EQ(voices_[1].Volume(), 24.0F / 64.0F);
  EXPECT_FLOAT_EQ(voices_[2].Volume(), 0.0F);
}

TEST_F(MusicPlayerTest, ReleaseSilencesTheChannel) {
  Write(0, 0, 0, {.period = 428, .sample = 2});
  Write(0, 1, 0, {.period = ModuleNote::kRelease});
  Play();
  Ticks(MusicPlayer::kTicksPerRow);
  EXPECT_FLOAT_EQ(voices_[0].Volume(), 0.0F);
}

TEST_F(MusicPlayerTest, ArpeggioCyclesThroughTemperedIntervals) {
  Write(0, 0, 0, {.period = 428, .sample = 1, .effect = 1, .parameter = 0x47});
  Play();
  const auto expected = std::array<float, 5>{4.0F, 7.0F, 0.0F, 7.0F, 4.0F};
  for (const auto semitones : expected) {
    Ticks(1);
    EXPECT_NEAR(voices_[0].Step(), ExpectedStep(428.0F * std::exp2(-semitones / 12.0F)), kTolerance);
  }
}

TEST_F(MusicPlayerTest, SlidesStopAtTheTemperedTarget) {
  Write(0, 0, 0, {.period = 428, .sample = 2, .effect = 8, .parameter = 0x2a});
  Play();
  Ticks(1);
  EXPECT_NEAR(voices_[0].Step(), ExpectedStep(418.0F), kTolerance);
  Ticks(10);  // into the next row, which has no note: the slide carries on
  EXPECT_NEAR(voices_[0].Step(), ExpectedStep(428.0F * std::exp2(-2.0F / 12.0F)), kTolerance);
}

TEST_F(MusicPlayerTest, PitchBendMovesThePeriodEveryTick) {
  Write(0, 0, 0, {.period = 428, .sample = 1, .effect = 2, .parameter = 0x30});
  Write(0, 0, 1, {.period = 428, .sample = 1, .effect = 2, .parameter = 0x02});
  Play();
  Ticks(2);
  EXPECT_NEAR(voices_[0].Step(), ExpectedStep(434.0F), kTolerance);
  EXPECT_NEAR(voices_[1].Step(), ExpectedStep(424.0F), kTolerance);
}

TEST_F(MusicPlayerTest, NoEffectClearsTheEffect) {
  Write(0, 0, 0, {.period = 428, .sample = 1, .effect = 1, .parameter = 0x47});
  Write(0, 1, 0, {.period = ModuleNote::kNoEffect});
  Play();
  Ticks(MusicPlayer::kTicksPerRow + 1);
  EXPECT_NEAR(voices_[0].Step(), ExpectedStep(428.0F * std::exp2(-4.0F / 12.0F)), kTolerance);  // where row 0 left it
}

TEST_F(MusicPlayerTest, StopSilencesEverything) {
  Write(0, 0, 0, {.period = 428, .sample = 2});
  Play();
  player_.Stop();
  EXPECT_FALSE(player_.Playing());
  EXPECT_FALSE(voices_[0].Active());
  const auto row = player_.Row();
  for (auto sample = 0; sample < 48'000; ++sample) {
    player_.AdvanceSample();
  }
  EXPECT_EQ(player_.Row(), row);
}

}  // namespace
}  // namespace hp2
