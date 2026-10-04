#include "host/format/road_shapes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace hp2::host {
namespace {

std::expected<std::vector<ShapePoint>, RoadShapeError> Decode(const std::vector<std::uint8_t>& bytes) {
  return DecodeRoadShape(std::as_bytes(std::span{bytes}));
}

TEST(RoadShape, DecodesAClosedOutline) {
  // A triangle: count 3, so four points of {x, z, y}, the last repeating the first. The count's
  // high byte is ignored, as the game does, and the word after the outline is not read.
  const auto outline = Decode({0x7f, 0x03,                          //
                               0x00, 0x10, 0x00, 0x20, 0x00, 0x00,  //
                               0x00, 0x30, 0x00, 0x20, 0x00, 0x00,  //
                               0xff, 0xf0, 0x40, 0x00, 0x00, 0x00,  //
                               0x00, 0x10, 0x00, 0x20, 0x00, 0x00,  //
                               0xff, 0xff});
  ASSERT_TRUE(outline.has_value());
  ASSERT_EQ(outline->size(), 4U);
  EXPECT_EQ((*outline)[1], (ShapePoint{.x = 48, .y = 32}));
  EXPECT_EQ((*outline)[2], (ShapePoint{.x = -16, .y = 16384}));
  EXPECT_EQ(outline->front(), outline->back());
}

TEST(RoadShape, NoneIsEmpty) {
  const auto outline = Decode({0xff, 0xff});
  ASSERT_TRUE(outline.has_value());
  EXPECT_TRUE(outline->empty());
}

TEST(RoadShape, RejectsBadOutlines) {
  EXPECT_EQ(Decode({0x00}).error(), RoadShapeError::kTruncated);
  EXPECT_EQ(Decode({0x00, 0x01, 0x00, 0x10, 0x00, 0x20, 0x00, 0x00}).error(), RoadShapeError::kTruncated);
  // The last point is not the first.
  EXPECT_EQ(Decode({0x00, 0x01, 0x00, 0x10, 0x00, 0x20, 0x00, 0x00, 0x00, 0x11, 0x00, 0x20, 0x00, 0x00}).error(),
            RoadShapeError::kNotClosed);
  // A point above the ground.
  EXPECT_EQ(Decode({0x00, 0x01, 0x00, 0x10, 0x00, 0x20, 0x00, 0x05, 0x00, 0x10, 0x00, 0x20, 0x00, 0x05}).error(),
            RoadShapeError::kNotFlat);
}

}  // namespace
}  // namespace hp2::host
