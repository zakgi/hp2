#pragma once

// Music and sound playback. The main loop steps the engine with the elapsed time; every output
// frame that became due is mixed from the voices straight into the ring an output backend drains
// (host/audio_output.hpp on the host, the I2S transfer on the target).

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/music_module.hpp"
#include "core/music_player.hpp"
#include "core/spsc_queue.hpp"
#include "core/voice.hpp"

namespace hp2 {

// One stereo output frame, interleaved as the backends expect.
struct AudioFrame {
  std::int16_t left{};
  std::int16_t right{};
};
static_assert(sizeof(AudioFrame) == 2 * sizeof(std::int16_t));

// Four voices, two on each side, mixed one frame per due output sample into the ring. The music
// player runs from the same sample clock.
class AudioEngine {
 public:
  static constexpr std::uint32_t kSampleRate = 48'000;
  static constexpr std::size_t kRingLog2 = 13;
  static constexpr std::size_t kVoiceCount = MusicPlayer::kChannelCount;
  using Ring = SpscQueue<AudioFrame, kRingLog2>;

  AudioEngine() : music_(voices_, kSampleRate) {}

  void PlayMusic(const MusicModule& module) { music_.Play(module); }
  void StopMusic() { music_.Stop(); }
  [[nodiscard]] bool MusicPlaying() const { return music_.Playing(); }

  // Advances the clock by `delta_us` and renders every output frame that became due into the
  // ring. Frames that find the ring full are still rendered, so the music keeps time, but dropped
  // and counted.
  void Step(std::uint32_t delta_us);

  [[nodiscard]] Ring& Output() { return ring_; }
  [[nodiscard]] Voice& GetVoice(std::size_t index) { return voices_[index]; }
  // Frames dropped on a full ring since the last call.
  [[nodiscard]] std::uint32_t TakeDroppedFrames();

 private:
  struct Pan {
    float left;
    float right;
  };

  static constexpr std::uint64_t kMicrosecondsPerSecond = 1'000'000;
  // Sample units to int16 at half amplitude (2^7): the two voices of a side sum to the full range
  // at most, so the mix cannot clip.
  static constexpr float kVoiceScale = 128.0F;
  // The original's layout: voices 0 and 3 on the left, 1 and 2 on the right.
  static constexpr auto kPan = std::to_array<Pan>({{.left = 1.0F, .right = 0.0F},
                                                   {.left = 0.0F, .right = 1.0F},
                                                   {.left = 0.0F, .right = 1.0F},
                                                   {.left = 1.0F, .right = 0.0F}});
  static_assert(kPan.size() == kVoiceCount);

  void RenderFrame(AudioFrame& frame);

  Ring ring_{};
  std::array<Voice, kVoiceCount> voices_{};
  MusicPlayer music_;
  std::uint64_t clock_remainder_{};  // microseconds times the rate, below one frame
  std::uint32_t dropped_frames_{};
};

}  // namespace hp2
