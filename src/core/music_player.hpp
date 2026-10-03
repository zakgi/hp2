#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/music_module.hpp"
#include "core/voice.hpp"

namespace hp2 {

// Plays a 15-sample Soundtracker module (the title music) on four voices, one per channel. The
// tempo is the module's: a tick every `timer` counts of the reference timer clock, a row every
// 6 ticks, the channel effects on the ticks between rows. Pitches are periods of the reference
// sound clock, as floats; effects that move by semitones use equal-tempered ratios. The original's
// replay routine (MusicTick, 1:1d0e, and the routines it calls) is the reference for what the
// effects mean (docs/system.md, "Music"), not for how it drives the hardware.
class MusicPlayer {
 public:
  static constexpr std::size_t kChannelCount = MusicModule::kChannelCount;
  static constexpr std::uint16_t kTicksPerRow = 6;
  static constexpr std::uint8_t kFullVolume = 64;

  MusicPlayer(std::span<Voice, kChannelCount> voices, std::uint32_t sample_rate)
      : voices_(voices), sample_rate_(sample_rate) {}

  // Starts `module` from its first position; the first row plays at once.
  void Play(const MusicModule& module);
  // Silences every channel; nothing more plays until the next Play.
  void Stop();
  [[nodiscard]] bool Playing() const { return playing_; }

  // One output sample of time; runs the ticks that became due.
  void AdvanceSample();
  // One tick: a row on the first tick of each row, the channel effects on the others.
  void Tick();

  [[nodiscard]] std::size_t Position() const { return position_; }
  [[nodiscard]] std::size_t Row() const { return row_; }

 private:
  // What a channel plays: its instrument, the period of its note (the reference for arpeggios
  // and slides), the period sounding now, its volume, its effect for this row and its slide.
  struct Channel {
    ModuleSample sample;
    float note_period{};
    float period{};
    std::uint8_t volume{};
    std::uint8_t effect{};
    std::uint8_t parameter{};
    float slide_speed{};
    float slide_target{};
  };

  void PlayRow();
  void PlayNote(std::size_t index, const ModuleNote& note);
  void RunEffects(std::size_t index);
  void SetPeriod(std::size_t index, float period);
  void SetVolume(std::size_t index, std::uint8_t volume);
  // The note of channel `index` in row `row_` of the current position's pattern.
  [[nodiscard]] ModuleNote GetNote(std::size_t index) const;

  std::span<Voice, kChannelCount> voices_;
  std::uint32_t sample_rate_;
  MusicModule module_{};
  std::array<Channel, kChannelCount> channels_{};
  // Timer clock counts times the sample rate since the last tick.
  std::uint64_t timer_remainder_{};
  std::uint16_t tick_{};
  std::size_t position_{};
  std::size_t row_{};
  bool playing_{};
};

}  // namespace hp2
