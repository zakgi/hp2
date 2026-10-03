#include "host/format/road_map.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <span>

namespace hp2::host {

std::expected<RoadMap, RoadMapError> DecodeRoadMap(std::span<const std::uint8_t> file) {
  auto result = std::expected<RoadMap, RoadMapError>{RoadMap{}};
  if (file.size() != result->cells.size()) {
    result = std::unexpected{RoadMapError::kBadSize};
  } else if (std::ranges::any_of(file, [](std::uint8_t type) { return type >= kRoadCellTypeCount; })) {
    result = std::unexpected{RoadMapError::kUnknownCellType};
  } else {
    std::ranges::copy(file, result->cells.begin());
  }
  return result;
}

}  // namespace hp2::host
