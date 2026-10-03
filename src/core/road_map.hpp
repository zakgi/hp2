#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hp2 {

// Road cell types 0 (no road) to 12 (docs/renderer.md).
inline constexpr auto kRoadCellTypeCount = std::size_t{13};

// The road map (CARTE.BIN): the road cell type of every cell of a 64 x 64 grid.
struct RoadMapView {
  static constexpr auto kSize = std::size_t{64};

  // Row after row: cell (column, row) at row * kSize + column.
  std::span<const std::uint8_t> cells;

  [[nodiscard]] constexpr std::uint8_t GetCell(std::size_t column, std::size_t row) const {
    return cells[(row * kSize) + column];
  }
};

// One scenery object of a road cell, in the cell's coordinates.
struct PlacedObject {
  std::int16_t x{};
  std::int16_t y{};
  std::int16_t z{};
  // 1 cactus, 3-6 stones, 7 bush, 8-10 road signs, 11 station sign (docs/vehicles.md).
  std::uint16_t type{};
  // The station sign's angle; 0 for the others.
  std::uint16_t extra{};

  friend constexpr bool operator==(const PlacedObject&, const PlacedObject&) = default;
};

// The elements [first, first + count) of a span.
struct IndexRange {
  std::uint32_t first{};
  std::uint32_t count{};

  friend constexpr bool operator==(const IndexRange&, const IndexRange&) = default;
};

// The scenery of every road cell type (COOR_OBJ.BIN): every cell of a type has the same objects,
// objects[first, first + count) of its range.
struct Scenery {
  std::span<const PlacedObject> objects;
  std::array<IndexRange, kRoadCellTypeCount> cell_types{};

  [[nodiscard]] constexpr std::span<const PlacedObject> GetObjects(std::size_t cell_type) const {
    return objects.subspan(cell_types[cell_type].first, cell_types[cell_type].count);
  }
};

}  // namespace hp2
