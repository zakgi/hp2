#include "host/format/road_map.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "assets.hpp"

namespace hp2::host {
namespace {

TEST(RoadMap, RejectsBadMaps) {
  EXPECT_EQ(DecodeRoadMap(std::vector<std::uint8_t>(4095)).error(), RoadMapError::kBadSize);
  auto file = std::vector<std::uint8_t>(4096);
  file[100] = 13;
  EXPECT_EQ(DecodeRoadMap(file).error(), RoadMapError::kUnknownCellType);
}

TEST(RoadMap, ReadsRowAfterRow) {
  auto file = std::vector<std::uint8_t>(4096);
  file[(2 * 64) + 5] = 12;
  const auto map = DecodeRoadMap(file);
  ASSERT_TRUE(map.has_value());
  EXPECT_EQ(map->Cell(5, 2), 12);
  EXPECT_EQ(map->Cell(2, 5), 0);
}

TEST(RoadMap, DecodesTheGamesMap) {
  const auto file = test::DataFile("CARTE.BIN");
  if (not file) {
    GTEST_SKIP() << "game disk not present at " << test::DiskImage();
  }
  const auto map = DecodeRoadMap(*file);
  ASSERT_TRUE(map.has_value());
  // 20 station cells: types 11 and 12 (docs/game.md).
  EXPECT_EQ(std::ranges::count(map->cells, 11) + std::ranges::count(map->cells, 12), 20);
  EXPECT_EQ(std::ranges::count(map->cells, 0), 3'272);
}

}  // namespace
}  // namespace hp2::host
