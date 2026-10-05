#include "core/road.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

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

// Where a lane leaving a cell by `exit` ends: on that side, to the right of the center line.
WorldPoint GetLaneEnd(Cell cell, Side exit) {
  const auto outward = GetOutward(exit);
  const auto right = WorldPoint{.x = outward.y, .y = -outward.x};
  return Inside(cell, 8192, 8192) + (outward * 8192.0F) + (right * 256.0F);
}

// Whether the road holds a car's width around every point of `lane`. A station's outline leaves a
// strip one unit wide outside the road where the driveway comes back to it, so a point there
// counts when the road is two units away.
void ExpectOnRoad(const Road& road, const Lane& lane, Cell cell) {
  constexpr auto kHalfWidth = 150.0F;
  constexpr auto kStep = 64.0F;
  const auto steps = static_cast<int>(lane.GetLength() / kStep);
  for (auto step = 0; step <= steps; ++step) {
    const auto distance = static_cast<float>(step) * kStep;
    const auto heading = lane.GetHeading(distance);
    const auto right = WorldPoint{.x = std::sin(heading), .y = -std::cos(heading)};
    for (const auto offset : {-kHalfWidth, 0.0F, kHalfWidth}) {
      const auto point = lane.GetPoint(distance) + (right * offset);
      const auto on_road = road.IsOnRoad(point) or road.IsOnRoad(point + WorldPoint{.x = 2.0F}) or
                           road.IsOnRoad(point + WorldPoint{.x = -2.0F}) or
                           road.IsOnRoad(point + WorldPoint{.y = 2.0F}) or
                           road.IsOnRoad(point + WorldPoint{.y = -2.0F});
      ASSERT_TRUE(on_road) << "cell " << cell.x << "," << cell.y << " at " << distance << " offset " << offset;
    }
  }
}

TEST_F(RoadTest, ALaneKeepsToTheRightOfTheCenterLine) {
  const auto road = MakeRoad();
  const auto straight = Cell{.x = 13, .y = 2};
  const auto north = road.GetLane(straight, Side::kSouth, Side::kNorth);
  ASSERT_TRUE(north);
  EXPECT_FLOAT_EQ(north->GetLength(), kCellUnits);
  EXPECT_FLOAT_EQ(north->GetPoint(0.0F).x, Inside(straight, 8448, 0).x);
  EXPECT_FLOAT_EQ(north->GetPoint(kCellUnits).y, Inside(straight, 8448, 16384).y);
  EXPECT_FLOAT_EQ(north->GetHeading(100.0F), kFullTurn / 4.0F);
  const auto south = road.GetLane(straight, Side::kNorth, Side::kSouth);
  ASSERT_TRUE(south);
  EXPECT_FLOAT_EQ(south->GetPoint(0.0F).x, Inside(straight, 7936, 0).x);
  // No lane through a closed side, or back out the way in.
  EXPECT_FALSE(road.GetLane(straight, Side::kSouth, Side::kEast));
  EXPECT_FALSE(road.GetLane(straight, Side::kSouth, Side::kSouth));
}

TEST_F(RoadTest, EveryLaneCrossesItsCellOnTheRoad) {
  const auto road = MakeRoad();
  auto lanes = 0;
  for (auto row = std::int16_t{0}; row < 40; ++row) {
    for (auto column = std::int16_t{0}; column < 40; ++column) {
      const auto cell = Cell{.x = column, .y = row};
      for (const auto entry : kSides) {
        for (const auto exit : kSides) {
          const auto lane = road.GetLane(cell, entry, exit);
          const auto exits = road.GetExits(cell);
          ASSERT_EQ(lane.has_value(), entry != exit and HasSide(exits, entry) and HasSide(exits, exit));
          if (lane) {
            ++lanes;
            const auto end = lane->GetPoint(lane->GetLength());
            ASSERT_NEAR(end.x, GetLaneEnd(cell, exit).x, 1.0F) << "cell " << column << "," << row;
            ASSERT_NEAR(end.y, GetLaneEnd(cell, exit).y, 1.0F) << "cell " << column << "," << row;
            ASSERT_NEAR(std::abs(WrapAngle(lane->GetHeading(lane->GetLength()) - GetAngle(GetOutward(exit)))), 0.0F,
                        0.001F);
            ASSERT_NO_FATAL_FAILURE(ExpectOnRoad(road, *lane, cell));
          }
        }
      }
    }
  }
  EXPECT_GT(lanes, 1600);
}

TEST_F(RoadTest, ADrivewayPassesThePumpsAndComesBackToTheRoad) {
  const auto road = MakeRoad();
  for (const auto station : road.GetStations()) {
    for (const auto entry : kSides) {
      const auto driveway = road.GetDriveway(station, entry);
      ASSERT_EQ(driveway.has_value(), HasSide(road.GetExits(station), entry));
      if (driveway) {
        const auto length = driveway->GetLength();
        EXPECT_TRUE(road.IsInStationArea(driveway->GetPoint(length / 2.0F)));
        const auto end = driveway->GetPoint(length);
        EXPECT_NEAR(end.x, GetLaneEnd(station, GetOpposite(entry)).x, 1.0F);
        EXPECT_NEAR(end.y, GetLaneEnd(station, GetOpposite(entry)).y, 1.0F);
        ASSERT_NO_FATAL_FAILURE(ExpectOnRoad(road, *driveway, station));
      }
    }
  }
  EXPECT_FALSE(road.GetDriveway(Cell{.x = 13, .y = 2}, Side::kSouth));
}

TEST_F(RoadTest, ALaneLocatesAPointBesideIt) {
  const auto road = MakeRoad();
  // A left turn at a crossroads: straight, arc, straight.
  const auto lane = road.GetLane(Cell{.x = 25, .y = 5}, Side::kSouth, Side::kWest);
  ASSERT_TRUE(lane);
  const auto steps = static_cast<int>(lane->GetLength() / 500.0F);
  for (auto step = 0; step < steps; ++step) {
    const auto distance = 50.0F + (static_cast<float>(step) * 500.0F);
    const auto heading = lane->GetHeading(distance);
    const auto right = WorldPoint{.x = std::sin(heading), .y = -std::cos(heading)};
    const auto place = lane->Locate(lane->GetPoint(distance) + (right * 100.0F));
    EXPECT_NEAR(place.distance, distance, 1.0F);
    EXPECT_NEAR(place.offset, 100.0F, 1.0F);
  }
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
