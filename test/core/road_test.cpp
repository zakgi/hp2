#include "core/road.hpp"

#include <gtest/gtest.h>

#include "assets.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class RoadTest : public testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
  }
  [[nodiscard]] Road MakeRoad() const {
    const auto& assets = manager_.Engine();
    return Road{assets.road_map, assets.road_shapes, assets.scenery};
  }
  host::AssetManager manager_;
};

// The point `east`, `north` units into `cell`.
WorldPoint Inside(Cell cell, float east, float north) {
  return GetCellOrigin(cell) + WorldPoint{.x = east, .y = north};
}

TEST_F(RoadTest, FindsTheStationsInTheOriginalsOrder) {
  const auto road = MakeRoad();
  const auto stations = road.GetStations();
  ASSERT_EQ(stations.size(), kStationCount);
  EXPECT_EQ(stations.front(), (Cell{.x = 4, .y = 1}));
  EXPECT_EQ(stations.back(), (Cell{.x = 25, .y = 38}));
  EXPECT_EQ(road.GetStationIndex(Cell{.x = 32, .y = 5}), 1U);
  EXPECT_FALSE(road.GetStationIndex(Cell{.x = 13, .y = 2}));
}

TEST_F(RoadTest, ReadsCellTypesAndExits) {
  const auto road = MakeRoad();
  EXPECT_EQ(road.GetCellType(Cell{.x = 13, .y = 2}), 1);
  EXPECT_EQ(road.GetExits(Cell{.x = 13, .y = 2}), 0xc);   // north and south
  EXPECT_EQ(road.GetCellType(Cell{.x = -1, .y = 2}), 0);  // off the map
  EXPECT_EQ(road.GetCellType(Cell{.x = 64, .y = 2}), 0);
}

TEST_F(RoadTest, TellsRoadFromDesert) {
  const auto road = MakeRoad();
  const auto straight = Cell{.x = 13, .y = 2};  // north-south, x 7680..8704
  EXPECT_TRUE(road.IsOnRoad(Inside(straight, 8192, 8192)));
  EXPECT_TRUE(road.IsOnRoad(Inside(straight, 7700, 100)));
  EXPECT_FALSE(road.IsOnRoad(Inside(straight, 8800, 8192)));
  EXPECT_FALSE(road.IsOnRoad(Inside(Cell{.x = 0, .y = 0}, 8192, 8192)));
}

TEST_F(RoadTest, StationDrivewaysLoopAroundAnIsland) {
  const auto road = MakeRoad();
  // Type 11: the driveway on the east side.
  const auto station = Cell{.x = 32, .y = 5};
  EXPECT_TRUE(road.IsOnRoad(Inside(station, 8192, 8192)));    // the road
  EXPECT_FALSE(road.IsOnRoad(Inside(station, 10000, 8192)));  // the island
  EXPECT_TRUE(road.IsOnRoad(Inside(station, 13824, 8192)));   // at the pumps
  EXPECT_TRUE(road.IsInStationArea(Inside(station, 13824, 8192)));
  EXPECT_FALSE(road.IsInStationArea(Inside(station, 8192, 8192)));
}

}  // namespace
}  // namespace hp2
