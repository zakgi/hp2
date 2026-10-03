#include "core/music_player.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hp2 {

namespace {

enum Effect : std::uint8_t {
  kArpeggio = 1,
  kPitchBend = 2,
  kVolumeUp = 5,
  kVolumeDown = 6,
  kSlideDown = 7,
  kSlideUp = 8,
};

// Arpeggio notes by tick within a row (ticks 1..5): the parameter's high nibble in semitones, its
// low nibble, the note itself, the low nibble, the high nibble.
enum class ArpeggioNote : std::uint8_t { kHigh, kLow, kBase };
constexpr auto kArpeggioNotes = std::to_array<ArpeggioNote>(
    {ArpeggioNote::kHigh, ArpeggioNote::kLow, ArpeggioNote::kBase, ArpeggioNote::kLow, ArpeggioNote::kHigh});

constexpr auto kNibble = std::uint8_t{0xf};
constexpr auto kSemitonesPerOctave = 12.0F;

std::uint8_t High(std::uint8_t parameter) {
  return static_cast<std::uint8_t>(parameter >> 4);
}

std::uint8_t Low(std::uint8_t parameter) {
  return static_cast<std::uint8_t>(parameter & kNibble);
}

// The period `semitones` higher in pitch (lower in pitch when negative), equal-tempered.
float Transpose(float period, float semitones) {
  return period * std::exp2(-semitones / kSemitonesPerOctave);
}

}  // namespace

void MusicPlayer::Play(const MusicModule& module) {
  Stop();
  module_ = module;
  channels_ = {};
  timer_remainder_ = 0;
  tick_ = 0;
  position_ = 0;
  row_ = 0;
  playing_ = not module.positions.empty() and module.timer != 0;
  if (playing_) {
    Tick();
  }
}

void MusicPlayer::Stop() {
  playing_ = false;
  for (auto& voice : voices_) {
    voice.SetVolume(0.0F);
    voice.Stop();
  }
}

void MusicPlayer::AdvanceSample() {
  if (playing_) {
    const auto tick_length = std::uint64_t{module_.timer} * sample_rate_;
    timer_remainder_ += kMusicTimerClockHz;
    while (timer_remainder_ >= tick_length) {
      timer_remainder_ -= tick_length;
      Tick();
    }
  }
}

void MusicPlayer::Tick() {
  if (tick_ == 0) {
    PlayRow();
  } else {
    for (auto index = std::size_t{0}; index < kChannelCount; ++index) {
      RunEffects(index);
    }
  }
  tick_ = static_cast<std::uint16_t>((tick_ + 1) % kTicksPerRow);
}

ModuleNote MusicPlayer::GetNote(std::size_t index) const {
  auto note = ModuleNote{};
  if (position_ < module_.positions.size()) {
    const auto offset =
        (std::size_t{module_.positions[position_]} * MusicModule::kPatternNotes) + (row_ * kChannelCount) + index;
    if (offset < module_.notes.size()) {
      note = module_.notes[offset];
    }
  }
  return note;
}

void MusicPlayer::PlayRow() {
  for (auto index = std::size_t{0}; index < kChannelCount; ++index) {
    PlayNote(index, GetNote(index));
  }
  ++row_;
  if (row_ == MusicModule::kRowCount) {
    row_ = 0;
    position_ = (position_ + 1) % module_.positions.size();
  }
}

void MusicPlayer::PlayNote(std::size_t index, const ModuleNote& note) {
  auto& channel = channels_[index];
  const auto clears_effect = note.period == ModuleNote::kNoEffect;
  channel.effect = clears_effect ? std::uint8_t{0} : note.effect;
  channel.parameter = clears_effect ? std::uint8_t{0} : note.parameter;
  if (not clears_effect and note.sample != 0 and note.sample <= module_.samples.size()) {
    channel.sample = module_.samples[note.sample - 1];
    auto volume = static_cast<int>(channel.sample.volume);
    if (note.effect == kVolumeUp) {
      volume = std::min(volume + note.parameter, int{kFullVolume});
    } else if (note.effect == kVolumeDown) {
      volume = std::max(volume - note.parameter, 0);
    }
    SetVolume(index, static_cast<std::uint8_t>(volume));
  }
  if (note.period == ModuleNote::kRelease) {
    channel.slide_speed = 0.0F;
    SetVolume(index, 0);
  } else if (note.period != 0 and not clears_effect) {
    channel.slide_speed = 0.0F;
    channel.note_period = static_cast<float>(note.period);
    SetPeriod(index, channel.note_period);
    // The head plays once, then the loop, which in this format follows the head or covers it.
    const auto& sample = channel.sample;
    const auto data = module_.GetSampleData(sample);
    const auto head = std::min<std::size_t>(sample.length, data.size());
    const auto loop_end = std::min<std::size_t>(std::size_t{sample.loop_start} + sample.loop_length, data.size());
    if (sample.Loops() and loop_end > sample.loop_start) {
      voices_[index].Start(data.first(std::max(head, loop_end)), sample.loop_start, loop_end);
    } else {
      voices_[index].Start(data.first(head), 0, 0);
    }
  }
  // A slide starts from the period sounding now and runs until the next note.
  if ((channel.effect == kSlideDown or channel.effect == kSlideUp) and channel.slide_speed == 0.0F) {
    const auto upward = channel.effect == kSlideUp;
    const auto semitones = static_cast<float>(High(channel.parameter));
    const auto speed = static_cast<float>(Low(channel.parameter));
    channel.slide_speed = upward ? -speed : speed;
    channel.slide_target = Transpose(channel.period, upward ? semitones : -semitones);
  }
}

void MusicPlayer::RunEffects(std::size_t index) {
  auto& channel = channels_[index];
  if (channel.slide_speed != 0.0F) {
    const auto moved = channel.period + channel.slide_speed;
    SetPeriod(index, channel.slide_speed > 0.0F ? std::min(moved, channel.slide_target)
                                                : std::max(moved, channel.slide_target));
  } else if (channel.effect == kArpeggio) {
    const auto which = kArpeggioNotes[static_cast<std::size_t>(tick_ - 1) % kArpeggioNotes.size()];
    auto semitones = std::uint8_t{0};
    if (which == ArpeggioNote::kHigh) {
      semitones = High(channel.parameter);
    } else if (which == ArpeggioNote::kLow) {
      semitones = Low(channel.parameter);
    }
    SetPeriod(index, Transpose(channel.note_period, static_cast<float>(semitones)));
  } else if (channel.effect == kPitchBend) {
    // The high nibble bends down, else the low nibble up, by that much period per tick.
    const auto bend = High(channel.parameter) != 0 ? static_cast<float>(High(channel.parameter))
                                                   : -static_cast<float>(Low(channel.parameter));
    SetPeriod(index, channel.period + bend);
  }
}

void MusicPlayer::SetPeriod(std::size_t index, float period) {
  constexpr auto kLowestPeriod = 1.0F;
  channels_[index].period = std::max(period, kLowestPeriod);
  voices_[index].SetStep(static_cast<float>(kMusicSoundClockHz) /
                         (channels_[index].period * static_cast<float>(sample_rate_)));
}

void MusicPlayer::SetVolume(std::size_t index, std::uint8_t volume) {
  channels_[index].volume = volume;
  voices_[index].SetVolume(static_cast<float>(volume) / static_cast<float>(kFullVolume));
}

}  // namespace hp2
