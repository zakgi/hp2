#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "core/road_map.hpp"

namespace hp2::host {

enum class RoadMapError : std::uint8_t {
  kBadSize,
  kUnknownCellType,
};

// The road map (CARTE.BIN): one road cell type per cell of a 64 x 64 grid.
struct RoadMap {
  static constexpr auto kSize = std::size_t{64};

  // Row after row: cell (column, row) at row * kSize + column.
  std::array<std::uint8_t, kSize * kSize> cells{};

  [[nodiscard]] std::uint8_t Cell(std::size_t column, std::size_t row) const { return cells[(row * kSize) + column]; }
};

// Decodes CARTE.BIN (docs/formats.md, "Map: CARTE.BIN").
[[nodiscard]] std::expected<RoadMap, RoadMapError> DecodeRoadMap(std::span<const std::uint8_t> file);

}  // namespace hp2::host
