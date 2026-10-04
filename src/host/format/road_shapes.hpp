#pragma once

#include <cstdint>
#include <expected>
#include <vector>

#include "core/road_map.hpp"
#include "host/format/endian.hpp"

namespace hp2::host {

enum class RoadShapeError : std::uint8_t {
  kTruncated,
  kNotClosed,
  kNotFlat,
};

// Decodes one road outline from the start of `bytes`, as TranslatePolygon3D (0:1c98) reads it: a
// word count n (low byte; 0xffff for none), then n + 1 points of words {x, z, y}, the last equal to
// the first. x and z are kept; y must be 0, as it is for every road shape. Bytes past the outline
// are ignored.
[[nodiscard]] std::expected<std::vector<ShapePoint>, RoadShapeError> DecodeRoadShape(ConstDataSpan bytes);

}  // namespace hp2::host
