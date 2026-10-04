#pragma once

#include <cstdint>
#include <expected>
#include <vector>

#include "core/palette.hpp"
#include "host/format/amiga_hunk.hpp"
#include "host/format/color.hpp"

namespace hp2::host {

enum class PaletteListError : std::uint8_t {
  kOutOfHunk,
  kPastLastRegister,
  kTooManySegments,
};

// Reads the copper palette list at `offset` in `hunk` (InstallPalette, 1:19b4): repeated
// {word first color, word count, word lines to the next segment (0 = last), word colors[count]}.
// The first segment starts at screen row 0, each later one `lines` rows below the previous one.
// The colors, stored in `format`, come out decoded.
[[nodiscard]] std::expected<std::vector<PaletteSegment>, PaletteListError> ReadPaletteList(const LoadableHunk& hunk,
                                                                                           std::uint32_t offset,
                                                                                           ColorFormat format);

}  // namespace hp2::host
