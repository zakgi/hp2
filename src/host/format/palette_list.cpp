#include "host/format/palette_list.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace hp2::host {

namespace {

// A list longer than this is taken as a misread offset rather than a palette (the longest in the
// game has 39 segments).
constexpr auto kMaxSegments = std::size_t{256};
constexpr auto kSegmentHeaderBytes = std::uint32_t{6};

}  // namespace

std::expected<std::vector<Rgb>, PaletteListError> ReadPaletteList(const LoadableHunk& hunk, std::uint32_t offset,
                                                                  ColorFormat format) {
  auto result = std::expected<std::vector<Rgb>, PaletteListError>{std::vector<Rgb>{}};
  auto colors = std::array<Rgb, kColorRegisterCount>{};
  auto finished = false;
  while (result and not finished) {
    const auto first = hunk.Read<std::uint16_t>(offset);
    const auto count = hunk.Read<std::uint16_t>(offset + 2);
    const auto lines = hunk.Read<std::uint16_t>(offset + 4);
    if (not first or not count or not lines) {
      result = std::unexpected{PaletteListError::kOutOfHunk};
    } else if (std::size_t{*first} + *count > kColorRegisterCount) {
      result = std::unexpected{PaletteListError::kPastLastRegister};
    } else if (result->size() == kMaxSegments * kColorRegisterCount) {
      result = std::unexpected{PaletteListError::kTooManySegments};
    } else {
      for (auto index = std::uint32_t{0}; index < *count and result; ++index) {
        const auto color = hunk.Read<std::uint16_t>(offset + kSegmentHeaderBytes + (2 * index));
        if (color) {
          colors[*first + index] = FromColorWord(*color, format);
        } else {
          result = std::unexpected{PaletteListError::kOutOfHunk};
        }
      }
      if (result) {
        result->insert(result->end(), colors.begin(), colors.end());
      }
      offset += kSegmentHeaderBytes + (2 * std::uint32_t{*count});
      finished = *lines == 0;
    }
  }
  return result;
}

}  // namespace hp2::host
