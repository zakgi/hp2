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
// Each segment comes out as a set of kColorRegisterCount colors decoded from `format`: the colors in
// effect from that segment on, the registers it leaves keeping the previous set's.
[[nodiscard]] std::expected<std::vector<Rgb>, PaletteListError> ReadPaletteList(const LoadableHunk& hunk,
                                                                                std::uint32_t offset,
                                                                                ColorFormat format);

}  // namespace hp2::host
