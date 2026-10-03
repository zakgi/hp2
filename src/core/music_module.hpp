#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace hp2 {

// The clocks a module's numbers are counted in, those of the PAL Amiga it was written on: a
// period divides the sound clock into a sample rate, and the tempo is a count of the timer clock.
inline constexpr auto kMusicSoundClockHz = std::uint32_t{3'546'895};
inline constexpr auto kMusicTimerClockHz = std::uint32_t{709'379};

// One instrument of a 15-sample Soundtracker module (docs/formats.md, "Music: .MUS"): its bytes
// are sample_data[offset, offset + size) of the module.
struct ModuleSample {
  std::uint32_t offset{};
  std::uint32_t size{};
  // Bytes played when a note starts; then, for a looping instrument, the loop
  // [loop_start, loop_start + loop_length) repeats.
  std::uint32_t length{};
  std::uint32_t loop_start{};
  std::uint32_t loop_length{};
  // 0..64.
  std::uint8_t volume{};

  // A loop of one word is the format's "no loop".
  [[nodiscard]] constexpr bool Loops() const { return loop_length > 2; }

  friend constexpr bool operator==(const ModuleSample&, const ModuleSample&) = default;
};

// One channel's entry of a pattern row.
struct ModuleNote {
  // A period starts a note; 0 leaves the channel playing.
  static constexpr std::uint16_t kRelease = 0xfffe;   // silences the channel
  static constexpr std::uint16_t kNoEffect = 0xfffd;  // clears the channel's effect, nothing else

  std::uint16_t period{};
  // 1..15; 0 keeps the channel's instrument and volume.
  std::uint8_t sample{};
  std::uint8_t effect{};
  std::uint8_t parameter{};

  friend constexpr bool operator==(const ModuleNote&, const ModuleNote&) = default;
};

// A 15-sample Soundtracker module: the instruments, the order the patterns play in, the patterns
// (64 rows of 4 channels each, pattern after pattern) and the tempo, in timer clock counts per
// tick.
struct MusicModule {
  static constexpr std::size_t kSampleCount = 15;
  static constexpr std::size_t kChannelCount = 4;
  static constexpr std::size_t kRowCount = 64;
  static constexpr std::size_t kPatternNotes = kRowCount * kChannelCount;

  // The instruments' bytes, signed 8-bit, and the instruments: kSampleCount of them, sample n of a
  // note being samples[n - 1].
  std::span<const std::int8_t> sample_data;
  std::span<const ModuleSample> samples;
  std::span<const std::uint8_t> positions;
  std::span<const ModuleNote> notes;
  std::uint16_t timer{};

  [[nodiscard]] constexpr std::span<const std::int8_t> GetSampleData(const ModuleSample& sample) const {
    return sample_data.subspan(sample.offset, sample.size);
  }
};

}  // namespace hp2
