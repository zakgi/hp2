// The resampling kernel and the voice: phase, loops, edges, volume.

#include "core/voice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hp2 {
namespace {

using Kernel = ResamplerKernel;
constexpr std::size_t kTaps = Kernel::kTaps;
constexpr float kTolerance = 1e-3F;

std::span<const std::int8_t, kTaps> SampleWindow(std::span<const std::int8_t> data, std::size_t first) {
  return data.subspan(first).first<kTaps>();
}

std::vector<std::int8_t> Ramp(std::size_t count, std::int8_t first, std::int8_t slope) {
  auto ramp = std::vector<std::int8_t>(count);
  for (std::size_t index = 0; index < count; ++index) {
    ramp[index] = static_cast<std::int8_t>(first + (slope * static_cast<std::int8_t>(index)));
  }
  return ramp;
}

std::vector<float> RenderAll(Voice& voice, std::size_t count) {
  auto out = std::vector<float>(count);
  std::ranges::generate(out, [&voice] { return voice.Render(); });
  return out;
}

TEST(Kernel, ConstantWindowIsReproduced) {
  const auto window = std::array<std::int8_t, kTaps>{37, 37, 37, 37, 37, 37};
  for (const auto fraction : {0.0F, 0.25F, 0.5F, 0.99F}) {
    EXPECT_NEAR(Kernel::Calculate(window, fraction), 37.0F, kTolerance);
  }
}

TEST(Kernel, LinearRampIsReproduced) {
  const auto window = std::array<std::int8_t, kTaps>{10, 20, 30, 40, 50, 60};
  for (const auto fraction : {0.0F, 0.25F, 0.5F, 0.75F}) {
    EXPECT_NEAR(Kernel::Calculate(window, fraction), 30.0F + (10.0F * fraction), kTolerance);
  }
}

TEST(Kernel, ContinuousAcrossSamples) {
  const auto data = std::array<std::int8_t, kTaps + 1>{-90, 17, 120, -33, 64, 5, -128};
  EXPECT_NEAR(Kernel::Calculate(SampleWindow(data, 0), 1.0F), Kernel::Calculate(SampleWindow(data, 1), 0.0F),
              kTolerance);
}

TEST(Kernel, StaysWithinTheWindow) {
  const auto window = std::array<std::int8_t, kTaps>{-128, 127, -128, 127, -128, 127};
  for (const auto fraction : {0.0F, 0.1F, 0.5F, 0.9F}) {
    const auto value = Kernel::Calculate(window, fraction);
    EXPECT_GE(value, -128.0F);
    EXPECT_LE(value, 127.0F);
  }
}

TEST(VoiceTest, OneShotPlaysTheRampThenStops) {
  const auto ramp = Ramp(32, 0, 1);
  auto voice = Voice{};
  voice.SetStep(1.0F);
  voice.SetVolume(1.0F);
  voice.Start(ramp, 0, 0);
  const auto out = RenderAll(voice, ramp.size());
  for (std::size_t index = Kernel::kTapsBefore; index + (kTaps - Kernel::kTapsBefore) <= ramp.size(); ++index) {
    EXPECT_NEAR(out[index], static_cast<float>(index), kTolerance) << "at " << index;
  }
  EXPECT_FALSE(voice.Active());
  EXPECT_EQ(voice.Render(), 0.0F);
}

TEST(VoiceTest, SingleCycleLoopIsPeriodic) {
  const auto cycle = std::array<std::int8_t, 8>{0, 40, 80, 120, 80, 40, 0, -40};
  auto voice = Voice{};
  voice.SetStep(1.0F);
  voice.SetVolume(1.0F);
  voice.Start(cycle, 0, cycle.size());
  const auto out = RenderAll(voice, 4 * cycle.size());
  for (std::size_t index = 0; index + cycle.size() < out.size(); ++index) {
    EXPECT_NEAR(out[index + cycle.size()], out[index], kTolerance) << "at " << index;
  }
  EXPECT_TRUE(voice.Active());
}

TEST(VoiceTest, OneShotHeadThenLoop) {
  const auto ramp = Ramp(16, 0, 5);
  constexpr std::size_t kLoopStart = 8;
  constexpr std::size_t kLoopEnd = 12;
  auto voice = Voice{};
  voice.SetStep(1.0F);
  voice.SetVolume(1.0F);
  voice.Start(ramp, kLoopStart, kLoopEnd);
  const auto out = RenderAll(voice, 40);
  // The head plays once, exactly where the window is inside the samples and
  // the phase is still before the loop (inside it, the taps before wrap too).
  for (std::size_t index = Kernel::kTapsBefore; index < kLoopStart; ++index) {
    EXPECT_NEAR(out[index], static_cast<float>(ramp[index]), kTolerance) << "at " << index;
  }
  // From the loop start on, the output repeats with the loop's length.
  for (std::size_t index = kLoopStart; index + (kLoopEnd - kLoopStart) < out.size(); ++index) {
    EXPECT_NEAR(out[index + (kLoopEnd - kLoopStart)], out[index], kTolerance) << "at " << index;
  }
}

TEST(VoiceTest, VolumeScalesTheOutput) {
  const auto flat = std::vector<std::int8_t>(32, 100);
  auto voice = Voice{};
  voice.SetStep(1.0F);
  voice.SetVolume(0.5F);
  voice.Start(flat, 0, 0);
  const auto out = RenderAll(voice, 16);
  EXPECT_NEAR(out[8], 50.0F, kTolerance);
}

TEST(VoiceTest, FractionalStepAdvancesThePhase) {
  const auto flat = std::vector<std::int8_t>(32, 1);
  auto voice = Voice{};
  voice.SetStep(0.5F);
  voice.SetVolume(1.0F);
  voice.Start(flat, 0, 0);
  RenderAll(voice, 4);
  EXPECT_FLOAT_EQ(voice.Phase(), 2.0F);
}

TEST(VoiceTest, StopSilencesAtOnce) {
  const auto flat = std::vector<std::int8_t>(32, 100);
  auto voice = Voice{};
  voice.SetStep(1.0F);
  voice.SetVolume(1.0F);
  voice.Start(flat, 0, 0);
  voice.Stop();
  EXPECT_FALSE(voice.Active());
  EXPECT_EQ(voice.Render(), 0.0F);
}

TEST(VoiceTest, StartTakesTheVolumeAtOnceAndChangesRamp) {
  const auto flat = std::vector<std::int8_t>(4096, 100);
  auto voice = Voice{};
  voice.SetStep(1.0F);
  voice.SetVolume(1.0F);
  voice.Start(flat, 0, flat.size());
  EXPECT_NEAR(RenderAll(voice, 8).back(), 100.0F, kTolerance);
  voice.SetVolume(0.0F);
  const auto out = RenderAll(voice, 4000);
  EXPECT_GT(out[10], 90.0F);                  // still sounding a few samples later
  EXPECT_NEAR(out.back(), 0.0F, kTolerance);  // silent within a few dozen milliseconds
}

}  // namespace
}  // namespace hp2
