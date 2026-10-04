#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "core/xor_animation.hpp"
#include "host/format/amiga_hunk.hpp"

namespace hp2::host {

enum class DifError : std::uint8_t {
  kTooShort,
  kFrameOutOfFile,
  kOddOffset,
  kRunPastScreen,
};

// One run of a delta frame as pixel XOR masks: whole 16-pixel groups from screen pixel `offset`
// (row * 320 + x) on.
struct DifRun {
  std::uint32_t offset{};
  std::vector<std::uint8_t> masks;
};

struct DifFrame {
  std::vector<DifRun> runs;
  // XOR words the frame holds in the file.
  std::uint32_t words{};
};

// Decodes a .DIF animation (docs/formats.md, "Animation: .DIF") the way ApplyDIFFrame (0:1102)
// reads it: long frame count, long offsets[count] from the file start, then per frame repeated
// {word count (0 = end), word byte offset, word xor[count]} into an Atari ST low-res screen. Each
// run is converted from plane words (4 interleaved per 16 pixels, 160 bytes a row) to pixel masks;
// XORing plane bits is XORing color index bits, so the masks apply to pixels directly.
[[nodiscard]] std::expected<std::vector<DifFrame>, DifError> DecodeDif(std::span<const std::uint8_t> file);

enum class PlayListError : std::uint8_t {
  kOutOfHunk,
  kUnknownFrame,
  kTooLong,
};

// Reads the play list at `offset` in `hunk` that PlayDIF (0:106e) walks: {word frame (1-based,
// 0 = end), word delay} pairs. Frames become 0-based and must be below `frame_count`.
[[nodiscard]] std::expected<std::vector<AnimationStep>, PlayListError> ReadPlayList(const LoadableHunk& hunk,
                                                                                    std::uint32_t offset,
                                                                                    std::size_t frame_count);

}  // namespace hp2::host
