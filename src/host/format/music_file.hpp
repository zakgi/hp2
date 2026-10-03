#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "core/music_module.hpp"

namespace hp2::host {

enum class MusicFileError : std::uint8_t {
  kTooShort,
  kBadSongLength,
  kPatternsOutOfFile,
  kSamplesOutOfFile,
};

// A decoded .MUS file; the asset manager makes a MusicModule of it.
struct MusicFile {
  std::uint16_t timer{};
  std::vector<std::uint8_t> positions;
  std::vector<ModuleNote> notes;
  std::vector<std::int8_t> sample_data;
  std::array<ModuleSample, MusicModule::kSampleCount> samples{};
};

// Decodes a .MUS file (docs/formats.md, "Music: .MUS"): 15 long instrument sizes, then a 15-sample
// Soundtracker module whose title holds the tempo, then the instruments' bytes back to back.
[[nodiscard]] std::expected<MusicFile, MusicFileError> DecodeMusicFile(std::span<const std::uint8_t> file);

}  // namespace hp2::host
