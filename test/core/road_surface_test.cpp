#include "core/road_surface.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "core/road_projection.hpp"
#include "core/screen.hpp"
#include "core/view_palette.hpp"

namespace hp2 {
namespace {

// The bottom row of the view, in the colors nearest the car.
constexpr auto kRow = std::int16_t{131};
constexpr auto kNearOffset = std::uint8_t{kGroundFirstOffset + (10 * kColorRegisterCount)};
constexpr auto kSand = static_cast<std::uint8_t>(kNearOffset + kSandColor);
constexpr auto kAsphalt = static_cast<std::uint8_t>(kNearOffset + kAsphaltColor);
constexpr auto kLine = static_cast<std::uint8_t>(kNearOffset + kLineColor);

class RoadSurfaceTest : public testing::Test {
 protected:
  void SetUp() override {
    rows_.horizon_row = 70;
    rows_.bottom_row = kRow;
    rows_.left_column = 0;
    rows_.right_column = 319;
    // 500 units ahead, 4 units to a pixel: the edge lines are 26 / 4, 7 pixels wide.
    auto& row = rows_.row[rows_.Index(kRow)];
    row.depth = 500.0F;
    row.scale = 0.25F;
    row.spans[0] = RoadSpan{.left = 100.4F, .right = 200.6F};
    row.span_count = 1;
  }
  [[nodiscard]] std::uint8_t GetPixel(std::int16_t column) const {
    return screen_.ScreenRow(static_cast<std::uint16_t>(kRow))[static_cast<std::size_t>(column)];
  }
  RoadRows rows_;
  Screen screen_;
};

TEST_F(RoadSurfaceTest, FillsSandAndTheSpansInTheRowsHaze) {
  // No dash at 500 + 0 units: the first lies from 0 to 420.
  DrawRoadSurface(rows_, 0.0F, screen_);
  EXPECT_EQ(GetPixel(0), kSand);
  EXPECT_EQ(GetPixel(99), kSand);
  EXPECT_EQ(GetPixel(100), kAsphalt);
  EXPECT_EQ(GetPixel(150), kAsphalt);
  EXPECT_EQ(GetPixel(200), kAsphalt);
  EXPECT_EQ(GetPixel(201), kSand);
  EXPECT_EQ(GetPixel(319), kSand);
  // The horizon's row is in the first, haziest set.
  EXPECT_EQ(screen_.ScreenRow(70)[0], kGroundFirstOffset + kSandColor);
}

TEST_F(RoadSurfaceTest, DrawsTheEdgeLinesWhereADashLies) {
  // Driven 600 units, the dash from 1050 to 1470 has come to 450..870: over this row.
  DrawRoadSurface(rows_, 600.0F, screen_);
  EXPECT_EQ(GetPixel(99), kSand);
  EXPECT_EQ(GetPixel(100), kLine);
  EXPECT_EQ(GetPixel(106), kLine);
  EXPECT_EQ(GetPixel(107), kAsphalt);
  EXPECT_EQ(GetPixel(193), kAsphalt);
  EXPECT_EQ(GetPixel(194), kLine);
  EXPECT_EQ(GetPixel(200), kLine);
  EXPECT_EQ(GetPixel(201), kSand);
}

TEST_F(RoadSurfaceTest, DrawsNoLinesFarAhead) {
  auto& row = rows_.row[rows_.Index(kRow)];
  row.depth = kLineDepth + 150.0F;
  // A dash lies there: 4450 is 250 into the period of 1050.
  DrawRoadSurface(rows_, 0.0F, screen_);
  EXPECT_EQ(GetPixel(100), kAsphalt);
  EXPECT_EQ(GetPixel(200), kAsphalt);
}

}  // namespace
}  // namespace hp2
