#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "core/road_map.hpp"

namespace hp2::host {

enum class ObjectPlacementError : std::uint8_t {
  kTooShort,
  kListOutOfFile,
};

// COOR_OBJ.BIN: the scenery of every road cell type; every cell of a type has the same.
struct ObjectPlacement {
  std::array<std::vector<PlacedObject>, kRoadCellTypeCount> cell_types;
};

// Decodes COOR_OBJ.BIN (docs/formats.md, "Object placement: COOR_OBJ.BIN") the way
// CopyTranslateObjects (0:22fa) reads it: a long offset per cell type, each to a list of a word
// count (low byte; 0xffff for none) and 10-byte entries {x, y, z, type, extra}.
[[nodiscard]] std::expected<ObjectPlacement, ObjectPlacementError> DecodeObjectPlacement(
    std::span<const std::uint8_t> file);

}  // namespace hp2::host
