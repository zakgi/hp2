#pragma once

// The host output backend: an SFML stream that drains the engine's ring from the audio thread, a
// chunk at a time, and pads with silence when the ring runs dry. Nothing here touches the voices
// or the music.

#include <SFML/Audio/SoundStream.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/audio_engine.hpp"

namespace hp2::host {

class AudioOutput : public sf::SoundStream {
 public:
  static constexpr std::size_t kChunkFrames = 256;
  // The startup fill: half the ring of silence goes in before the stream starts, so playback runs
  // with that much slack on either side.
  static constexpr std::size_t kPrimeFrames = AudioEngine::Ring::Capacity() / 2;

  // Fills the ring with kPrimeFrames of silence; construct before the engine steps.
  explicit AudioOutput(AudioEngine& engine);
  // The stream's thread reads chunk_: it stops before the members go.
  ~AudioOutput() override { stop(); }
  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  AudioOutput(AudioOutput&&) = delete;
  AudioOutput& operator=(AudioOutput&&) = delete;

  // Frames replaced with silence since the last call.
  [[nodiscard]] std::uint32_t TakeMissingFrames() { return missing_frames_.exchange(0, std::memory_order_relaxed); }

 private:
  bool onGetData(Chunk& data) override;
  void onSeek([[maybe_unused]] sf::Time time_offset) override {}

  AudioEngine::Ring& ring_;
  std::array<AudioFrame, kChunkFrames> chunk_{};
  std::atomic<std::uint32_t> missing_frames_{0};
};

}  // namespace hp2::host
