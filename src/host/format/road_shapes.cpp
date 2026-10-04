#include "host/format/road_shapes.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace hp2::host {

namespace {

constexpr auto kNoOutline = std::uint16_t{0xffff};
constexpr auto kCountMask = std::uint16_t{0xff};
constexpr auto kPointBytes = std::size_t{6};

}  // namespace

std::expected<std::vector<ShapePoint>, RoadShapeError> DecodeRoadShape(ConstDataSpan bytes) {
  auto result = std::expected<std::vector<ShapePoint>, RoadShapeError>{std::vector<ShapePoint>{}};
  const auto count_word = TakeBigEndian<std::uint16_t>(bytes);
  if (not count_word) {
    result = std::unexpected{RoadShapeError::kTruncated};
  } else if (*count_word != kNoOutline) {
    const auto count = static_cast<std::size_t>(*count_word & kCountMask) + 1;
    if (bytes.size() < count * kPointBytes) {
      result = std::unexpected{RoadShapeError::kTruncated};
    }
    for (auto index = std::size_t{0}; result and index < count; ++index) {
      // The shapes' x runs east, z north (the map's y), y up.
      const auto east = *TakeBigEndian<std::int16_t>(bytes);
      const auto north = *TakeBigEndian<std::int16_t>(bytes);
      const auto height = *TakeBigEndian<std::int16_t>(bytes);
      if (height != 0) {
        result = std::unexpected{RoadShapeError::kNotFlat};
      } else {
        result->push_back(ShapePoint{.x = east, .y = north});
      }
    }
    if (result and result->front() != result->back()) {
      result = std::unexpected{RoadShapeError::kNotClosed};
    }
  }
  return result;
}

}  // namespace hp2::host
