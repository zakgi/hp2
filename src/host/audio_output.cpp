#include "host/audio_output.hpp"

#include <SFML/Audio/SoundChannel.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace hp2::host {

AudioOutput::AudioOutput(AudioEngine& engine) : ring_(engine.Output()) {
  auto remaining = kPrimeFrames;
  while (remaining > 0) {
    auto slots = ring_.Reserve(remaining);
    if (slots.empty()) {
      remaining = 0;
    } else {
      std::ranges::fill(slots, AudioFrame{});
      ring_.Commit(slots.size());
      remaining -= slots.size();
    }
  }
  initialize(2, AudioEngine::kSampleRate, {sf::SoundChannel::FrontLeft, sf::SoundChannel::FrontRight});
}

bool AudioOutput::onGetData(Chunk& data) {
  const auto delivered = ring_.Pop(chunk_);
  const auto missing = chunk_.size() - delivered.size();
  if (missing > 0) {
    std::fill(chunk_.begin() + static_cast<std::ptrdiff_t>(delivered.size()), chunk_.end(), AudioFrame{});
    missing_frames_.fetch_add(static_cast<std::uint32_t>(missing), std::memory_order_relaxed);
  }
  // AudioFrame is two int16 samples, left then right, as SFML takes them.
  data.samples = reinterpret_cast<const std::int16_t*>(chunk_.data());
  data.sampleCount = chunk_.size() * 2;
  return true;
}

}  // namespace hp2::host
