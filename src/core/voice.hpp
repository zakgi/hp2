#pragma once

// One mono sample stream and the kernel that resamples it. A Voice is the playback primitive the
// music and the sound effects share: it knows a view of signed 8-bit samples, a loop region, a
// phase, a step and a volume, and nothing about what plays it.

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hp2 {

// A resampling kernel: `kTaps` consecutive source samples, `kTapsBefore` of them before the integer
// position, and the value at a fraction in [0, 1). Stateless: static members only.
template <typename T>
concept Resampler = requires(std::span<const typename T::SampleType, T::kTaps> window, float fraction) {
  { T::kTaps } -> std::convertible_to<std::size_t>;
  { T::kTapsBefore } -> std::convertible_to<std::size_t>;
  { T::Calculate(window, fraction) } -> std::same_as<float>;
};

// 6-point, 5th-order B-spline (Niemitalo, "Polynomial Interpolators for High-Quality Resampling of
// Oversampled Audio", 2001). The output is a convex combination of the window, so it stays within
// the window's range, and the DC gain is one.
template <typename Sample = std::int8_t>
struct BSpline6Point {
  using SampleType = Sample;
  static constexpr std::size_t kTaps = 6;
  static constexpr std::size_t kTapsBefore = 2;

  // NOLINTBEGIN(readability-magic-numbers): the polynomial's coefficients.
  static constexpr float Calculate(std::span<const Sample, kTaps> window, float fraction) {
    const auto ym2 = static_cast<float>(window[0]);
    const auto ym1 = static_cast<float>(window[1]);
    const auto yp0 = static_cast<float>(window[2]);
    const auto yp1 = static_cast<float>(window[3]);
    const auto yp2 = static_cast<float>(window[4]);
    const auto yp3 = static_cast<float>(window[5]);
    const auto even2 = ym2 + yp2;
    const auto even1 = ym1 + yp1;
    const auto odd2 = yp2 - ym2;
    const auto odd1 = yp1 - ym1;
    const auto coef0 = ((1.0F / 120.0F) * even2) + ((13.0F / 60.0F) * even1) + ((11.0F / 20.0F) * yp0);
    const auto coef1 = ((1.0F / 24.0F) * odd2) + ((5.0F / 12.0F) * odd1);
    const auto coef2 = ((1.0F / 12.0F) * even2) + ((1.0F / 6.0F) * even1) - ((1.0F / 2.0F) * yp0);
    const auto coef3 = ((1.0F / 12.0F) * odd2) - ((1.0F / 6.0F) * odd1);
    const auto coef4 = ((1.0F / 24.0F) * even2) - ((1.0F / 6.0F) * even1) + ((1.0F / 4.0F) * yp0);
    const auto coef5 =
        ((1.0F / 120.0F) * (yp3 - ym2)) + ((1.0F / 24.0F) * (ym1 - yp2)) + ((1.0F / 12.0F) * (yp1 - yp0));
    auto value = coef5;
    value = (value * fraction) + coef4;
    value = (value * fraction) + coef3;
    value = (value * fraction) + coef2;
    value = (value * fraction) + coef1;
    value = (value * fraction) + coef0;
    return value;
  }
  // NOLINTEND(readability-magic-numbers)
};

// The kernel every Voice uses: a build-time choice.
using ResamplerKernel = BSpline6Point<>;
static_assert(Resampler<ResamplerKernel>);

// A mono stream of signed 8-bit samples read through the kernel at a phase that advances by `step`
// source samples per output sample. A looping voice plays from the start to the loop's end, then
// repeats [loop_start, loop_end) until stopped; a one-shot voice stops at the end of its samples.
class Voice {
 public:
  using Sample = ResamplerKernel::SampleType;

  // Plays `samples` from the beginning, at the volume last set. A `loop_end` above `loop_start`
  // makes the voice loop that region, clipped to the samples' end.
  void Start(std::span<const Sample> samples, std::size_t loop_start, std::size_t loop_end);
  void Stop() { active_ = false; }
  void SetStep(float step) { step_ = step; }
  // The volume, 0..1. A sounding voice moves to it over a few milliseconds, since a step in
  // amplitude at the caller's rate is audible; Start takes it at once.
  void SetVolume(float volume) { target_volume_ = volume; }
  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] float Phase() const { return phase_; }
  [[nodiscard]] float Step() const { return step_; }
  [[nodiscard]] float Volume() const { return target_volume_; }

  // The next output sample in the source's units times the volume; the phase advances. Zero once
  // the voice is inactive.
  float Render();

 private:
  static constexpr std::size_t kTaps = ResamplerKernel::kTaps;
  static constexpr std::size_t kTapsBefore = ResamplerKernel::kTapsBefore;
  static constexpr std::size_t kTapsAfter = kTaps - kTapsBefore;

  // The window around `index` when a subspan will not do: zeros before the start and past a
  // one-shot's end; past the loop's end, the loop's start; and once the phase is inside the loop,
  // the loop's end before its start.
  [[nodiscard]] std::array<Sample, kTaps> EdgeWindow(std::size_t index, bool in_loop) const;

  std::span<const Sample> samples_;
  std::size_t loop_start_{};
  std::size_t loop_end_{};
  float phase_{};
  float step_{};
  float volume_{};
  float target_volume_{};
  bool looping_{};
  bool active_{};
};

}  // namespace hp2
