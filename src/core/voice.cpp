#include "core/voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace hp2 {

void Voice::Start(std::span<const Sample> samples, std::size_t loop_start, std::size_t loop_end) {
  samples_ = samples;
  loop_end_ = std::min(loop_end, samples.size());
  loop_start_ = std::min(loop_start, loop_end_);
  looping_ = loop_end_ > loop_start_;
  phase_ = 0.0F;
  volume_ = target_volume_;
  active_ = not samples.empty();
}

std::array<Voice::Sample, Voice::kTaps> Voice::EdgeWindow(std::size_t index, bool in_loop) const {
  auto window = std::array<Sample, kTaps>{};
  const auto first = static_cast<std::ptrdiff_t>(index) - static_cast<std::ptrdiff_t>(kTapsBefore);
  if (in_loop) {
    // Inside the loop the window is an arc of the circle [loop_start, loop_end).
    const auto length = static_cast<std::ptrdiff_t>(loop_end_ - loop_start_);
    auto offset = (first - static_cast<std::ptrdiff_t>(loop_start_)) % length;
    if (offset < 0) {
      offset += length;
    }
    auto position = loop_start_ + static_cast<std::size_t>(offset);
    auto filled = std::size_t{0};
    while (filled < kTaps) {
      const auto count = std::min(kTaps - filled, loop_end_ - position);
      std::ranges::copy(samples_.subspan(position, count), window.begin() + static_cast<std::ptrdiff_t>(filled));
      filled += count;
      position = loop_start_;
    }
  } else {
    // Before the loop, or a one-shot: the readable part with zeros around it, and past the loop's
    // end the rest from its start.
    const auto upper = looping_ ? loop_end_ : samples_.size();
    const auto begin = static_cast<std::size_t>(std::max<std::ptrdiff_t>(first, 0));
    const auto end = std::min(index + kTapsAfter, upper);
    const auto lead = static_cast<std::ptrdiff_t>(begin) - first;
    std::ranges::copy(samples_.subspan(begin, end - begin), window.begin() + lead);
    const auto filled = static_cast<std::size_t>(lead) + (end - begin);
    if (looping_ and end == loop_end_) {
      const auto count = std::min(kTaps - filled, loop_end_ - loop_start_);
      std::ranges::copy(samples_.subspan(loop_start_, count), window.begin() + static_cast<std::ptrdiff_t>(filled));
    }
  }
  return window;
}

float Voice::Render() {
  auto value = 0.0F;
  if (active_) {
    const auto index = static_cast<std::size_t>(phase_);
    const auto fraction = phase_ - static_cast<float>(index);
    const auto in_loop = looping_ and index >= loop_start_;
    const auto lower = in_loop ? loop_start_ : std::size_t{0};
    const auto upper = looping_ ? loop_end_ : samples_.size();
    if (index >= lower + kTapsBefore and index + kTapsAfter <= upper) {
      value = ResamplerKernel::Calculate(samples_.subspan(index - kTapsBefore).first<kTaps>(), fraction);
    } else {
      const auto window = EdgeWindow(index, in_loop);
      value = ResamplerKernel::Calculate(window, fraction);
    }
    // The volume ramps toward its target per sample: the caller sets it at its own rate, and a
    // step in amplitude at that rate is audible.
    constexpr float kVolumeRampRate = 0.0005F;
    if (volume_ < target_volume_) {
      volume_ = std::min(volume_ + kVolumeRampRate, target_volume_);
    } else if (volume_ > target_volume_) {
      volume_ = std::max(volume_ - kVolumeRampRate, target_volume_);
    }
    value *= volume_;
    phase_ += step_;
    if (looping_) {
      const auto loop_end = static_cast<float>(loop_end_);
      if (phase_ >= loop_end) {
        const auto loop_start = static_cast<float>(loop_start_);
        phase_ = loop_start + std::fmod(phase_ - loop_start, loop_end - loop_start);
      }
    } else if (phase_ >= static_cast<float>(samples_.size())) {
      active_ = false;
    }
  }
  return value;
}

}  // namespace hp2
