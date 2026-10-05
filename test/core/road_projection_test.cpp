#include "core/road_projection.hpp"

#include <gtest/gtest.h>

#include <cstddef>

#include "assets.hpp"
#include "core/road.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

constexpr auto kEyeHeight = 100.0F;
constexpr auto kNorth = kFullTurn / 4.0F;
constexpr auto kEast = 0.0F;

class RoadProjectionTest : public testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
  }
  // The rows seen from `east`, `north` units into `cell`, looking along `heading`.
  void Project(Cell cell, float east, float north, float heading) {
    const auto& assets = manager_.Engine();
    const auto road = Road{assets.road_map, assets.road_shapes, assets.scenery};
    const auto projection = RoadProjection{kDriverView, road};
    projection.Project(ViewInput{.position = GetCellOrigin(cell) + WorldPoint{.x = east, .y = north},
                                 .heading = heading,
                                 .eye_height = kEyeHeight},
                       rows_);
  }
  host::AssetManager manager_;
  RoadRows rows_;
};

// The north-south straight at (13, 2), its road from 7680 to 8704 east.
constexpr auto kStraight = Cell{.x = 13, .y = 2};

TEST_F(RoadProjectionTest, TheRoadAheadIsOneSpanAroundTheCenter) {
  Project(kStraight, 8448, 2000, kNorth);
  EXPECT_EQ(rows_.horizon_row, 70);
  EXPECT_EQ(rows_.bottom_row, 131);
  // From the third row down the rows hold this cell only: one span, the eye inside it.
  for (auto index = std::size_t{2}; index < kMaxViewRows; ++index) {
    const auto& row = rows_.row[index];
    ASSERT_EQ(row.span_count, 1) << "row " << index;
    EXPECT_LT(row.spans[0].left, kDriverView.center_column) << "row " << index;
    EXPECT_GT(row.spans[0].right, kDriverView.center_column) << "row " << index;
  }
  // The bottom row lies 416 units ahead: the road's edges, 768 units left and 256 right of the
  // eye, show 256 / 416 of that from the center.
  const auto& bottom = rows_.row[kMaxViewRows - 1];
  EXPECT_NEAR(bottom.depth, 416.26F, 0.01F);
  EXPECT_NEAR(bottom.spans[0].left, 160.0F - (768.0F * bottom.scale), 0.5F);
  EXPECT_NEAR(bottom.spans[0].right, 160.0F + (256.0F * bottom.scale), 0.5F);
}

TEST_F(RoadProjectionTest, TheRoadGoesOnIntoTheNextCell) {
  Project(kStraight, 8448, 2000, kNorth);
  // The second row holds the depths from 12800 to 25600: the cell ends 14384 ahead.
  const auto& row = rows_.row[1];
  auto ahead = false;
  for (const auto& span : row.GetSpans()) {
    ahead = ahead or (span.left < kDriverView.center_column and span.right > kDriverView.center_column);
  }
  EXPECT_TRUE(ahead);
  EXPECT_GT(rows_.row[0].span_count, 0);
}

TEST_F(RoadProjectionTest, ARoadAcrossShowsOnTheRowsOfItsDepth) {
  // Looking east from the desert west of the road: it lies from 4680 to 5704 ahead, on the rows
  // that hold 5120 to 6400 and 4267 to 5120, from one edge of the window to the other.
  Project(kStraight, 3000, 8000, kEast);
  EXPECT_EQ(rows_.row[3].span_count, 0);
  for (const auto index : {std::size_t{4}, std::size_t{5}}) {
    const auto& row = rows_.row[index];
    ASSERT_EQ(row.span_count, 1) << "row " << index;
    EXPECT_EQ(rows_.Column(row.spans[0].left), 0) << "row " << index;
    EXPECT_EQ(rows_.Column(row.spans[0].right), 320) << "row " << index;
  }
  EXPECT_EQ(rows_.row[6].span_count, 0);
}

TEST_F(RoadProjectionTest, TheDesertHasNoSpans) {
  // East of (38, 20) the map is empty.
  Project(Cell{.x = 38, .y = 20}, 8192, 8192, kEast);
  for (auto index = std::size_t{0}; index < kMaxViewRows; ++index) {
    EXPECT_EQ(rows_.row[index].span_count, 0) << "row " << index;
  }
}

TEST(RoadRows, AColumnIsTheFirstWhoseMiddleIsReached) {
  const auto rows = RoadRows{.horizon_row = 70, .bottom_row = 131, .left_column = 0, .right_column = 319, .row = {}};
  EXPECT_EQ(rows.Column(100.4F), 100);
  EXPECT_EQ(rows.Column(100.6F), 101);
  EXPECT_EQ(rows.Column(-50.0F), 0);
  EXPECT_EQ(rows.Column(900.0F), 320);
  EXPECT_EQ(rows.Index(72), 2U);
}

}  // namespace
}  // namespace hp2
