#include "host/format/music_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "host/format/endian.hpp"

namespace hp2::host {

namespace {

constexpr auto kSizeTableBytes = std::size_t{MusicModule::kSampleCount * 4};
// Offsets in the module, which starts after the size table.
constexpr auto kTimerOffset = std::size_t{0x4};  // inside the 20-byte title
constexpr auto kSampleHeadersOffset = std::size_t{0x14};
constexpr auto kSampleHeaderBytes = std::size_t{30};
constexpr auto kSampleNameBytes = std::size_t{22};
constexpr auto kSongLengthOffset = std::size_t{0x1d6};
constexpr auto kPositionsOffset = std::size_t{0x1d8};
constexpr auto kPositionCount = std::size_t{128};
constexpr auto kPatternsOffset = std::size_t{0x258};
constexpr auto kNoteBytes = std::size_t{4};
constexpr auto kPatternBytes = MusicModule::kPatternNotes * kNoteBytes;

std::uint16_t Word(ConstDataSpan bytes, std::size_t offset) {
  return *ReadBigEndian<std::uint16_t>(bytes.subspan(offset));
}

ModuleNote NoteFrom(ConstDataSpan bytes) {
  const auto control = std::to_integer<std::uint8_t>(bytes[2]);
  return ModuleNote{.period = Word(bytes, 0),
                    .sample = static_cast<std::uint8_t>(control >> 4),
                    .effect = static_cast<std::uint8_t>(control & 0xfU),
                    .parameter = std::to_integer<std::uint8_t>(bytes[3])};
}

}  // namespace

std::expected<MusicFile, MusicFileError> DecodeMusicFile(std::span<const std::uint8_t> file) {
  const auto bytes = std::as_bytes(file);
  auto result = std::expected<MusicFile, MusicFileError>{MusicFile{}};
  if (file.size() < kSizeTableBytes + kPatternsOffset) {
    result = std::unexpected{MusicFileError::kTooShort};
  } else {
    const auto module = bytes.subspan(kSizeTableBytes);
    const auto song_length = std::to_integer<std::size_t>(module[kSongLengthOffset]);
    const auto positions = file.subspan(kSizeTableBytes + kPositionsOffset, kPositionCount);
    if (song_length == 0 or song_length > kPositionCount) {
      result = std::unexpected{MusicFileError::kBadSongLength};
    } else {
      // As many patterns as the song's highest position names.
      const auto pattern_count = std::size_t{*std::ranges::max_element(positions.first(song_length))} + 1;
      const auto patterns_bytes = pattern_count * kPatternBytes;
      const auto samples_offset = kPatternsOffset + patterns_bytes;
      auto samples_bytes = std::size_t{0};
      for (auto index = std::size_t{0}; index < MusicModule::kSampleCount; ++index) {
        samples_bytes += *ReadBigEndian<std::uint32_t>(bytes.subspan(4 * index));
      }
      if (module.size() < samples_offset) {
        result = std::unexpected{MusicFileError::kPatternsOutOfFile};
      } else if (module.size() - samples_offset < samples_bytes) {
        result = std::unexpected{MusicFileError::kSamplesOutOfFile};
      } else {
        result->timer = Word(module, kTimerOffset);
        result->positions.assign(positions.begin(), positions.begin() + static_cast<std::ptrdiff_t>(song_length));
        for (auto offset = kPatternsOffset; offset < samples_offset; offset += kNoteBytes) {
          result->notes.push_back(NoteFrom(module.subspan(offset, kNoteBytes)));
        }
        const auto data = module.subspan(samples_offset, samples_bytes);
        result->sample_data.resize(samples_bytes);
        std::ranges::transform(data, result->sample_data.begin(),
                               [](std::byte value) { return std::to_integer<std::int8_t>(value); });
        auto offset = std::size_t{0};
        for (auto index = std::size_t{0}; index < MusicModule::kSampleCount; ++index) {
          const auto header = module.subspan(kSampleHeadersOffset + (index * kSampleHeaderBytes) + kSampleNameBytes);
          const auto size = std::size_t{*ReadBigEndian<std::uint32_t>(bytes.subspan(4 * index))};
          result->samples[index] =
              ModuleSample{.offset = static_cast<std::uint32_t>(offset),
                           .size = static_cast<std::uint32_t>(size),
                           .length = 2U * Word(header, 0),
                           .loop_start = Word(header, 4),
                           .loop_length = 2U * Word(header, 6),
                           .volume = static_cast<std::uint8_t>(std::min<std::uint16_t>(Word(header, 2) & 0xffU, 64))};
          offset += size;
        }
      }
    }
  }
  return result;
}

}  // namespace hp2::host
