#include "core/audio_engine.hpp"

#include <cstddef>
#include <cstdint>

namespace hp2 {

void AudioEngine::Step(std::uint32_t delta_us) {
  clock_remainder_ += std::uint64_t{delta_us} * kSampleRate;
  auto due = static_cast<std::size_t>(clock_remainder_ / kMicrosecondsPerSecond);
  clock_remainder_ %= kMicrosecondsPerSecond;
  while (due > 0) {
    auto slots = ring_.Reserve(due);
    if (slots.empty()) {
      auto discarded = AudioFrame{};
      for (; due > 0; --due) {
        music_.AdvanceSample();
        RenderFrame(discarded);
        ++dropped_frames_;
      }
    } else {
      for (auto& frame : slots) {
        music_.AdvanceSample();
        RenderFrame(frame);
      }
      ring_.Commit(slots.size());
      due -= slots.size();
    }
  }
}

std::uint32_t AudioEngine::TakeDroppedFrames() {
  const auto dropped = dropped_frames_;
  dropped_frames_ = 0;
  return dropped;
}

void AudioEngine::RenderFrame(AudioFrame& frame) {
  auto left = 0.0F;
  auto right = 0.0F;
  for (auto index = std::size_t{0}; index < kVoiceCount; ++index) {
    const auto value = voices_[index].Render();
    left += value * kPan[index].left;
    right += value * kPan[index].right;
  }
  frame.left = static_cast<std::int16_t>(left * kVoiceScale);
  frame.right = static_cast<std::int16_t>(right * kVoiceScale);
}

}  // namespace hp2
