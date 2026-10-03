#include "host/format/object_placement.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "assets.hpp"

namespace hp2::host {
namespace {

// 13 offsets to lists; every list but the first is empty (0xffff).
std::vector<std::uint8_t> OneList(const std::vector<std::uint8_t>& list) {
  auto file = std::vector<std::uint8_t>{};
  const auto first = 13 * 4;
  const auto empty = first + static_cast<int>(list.size());
  for (auto type = 0; type < 13; ++type) {
    const auto offset = type == 0 ? first : empty;
    file.insert(file.end(), {0, 0, static_cast<std::uint8_t>(offset >> 8), static_cast<std::uint8_t>(offset)});
  }
  file.insert(file.end(), list.begin(), list.end());
  file.insert(file.end(), {0xff, 0xff});
  return file;
}

TEST(ObjectPlacement, ReadsTheListOfEveryCellType) {
  // The count's high byte is ignored, as the game does.
  const auto placement =
      DecodeObjectPlacement(OneList({0x7f, 0x01, 0xff, 0xfe, 0x00, 0x10, 0x00, 0x20, 0x00, 0x0b, 0x01, 0x68}));
  ASSERT_TRUE(placement.has_value());
  ASSERT_EQ(placement->cell_types[0].size(), 1U);
  const auto& object = placement->cell_types[0][0];
  EXPECT_EQ(object.x, -2);
  EXPECT_EQ(object.y, 16);
  EXPECT_EQ(object.z, 32);
  EXPECT_EQ(object.type, 11);
  EXPECT_EQ(object.extra, 360);
  EXPECT_TRUE(placement->cell_types[12].empty());
}

TEST(ObjectPlacement, RejectsListsPastTheEnd) {
  EXPECT_EQ(DecodeObjectPlacement(std::vector<std::uint8_t>(51)).error(), ObjectPlacementError::kTooShort);
  EXPECT_EQ(DecodeObjectPlacement(OneList({0x00, 0x02, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0})).error(),
            ObjectPlacementError::kListOutOfFile);
}

TEST(ObjectPlacement, DecodesTheGamesScenery) {
  const auto file = test::DataFile("COOR_OBJ.BIN");
  if (not file) {
    GTEST_SKIP() << "game disk not present at " << test::DiskImage();
  }
  const auto placement = DecodeObjectPlacement(*file);
  ASSERT_TRUE(placement.has_value());
  const auto count = [&placement](std::size_t cell_type, std::uint16_t object_type) {
    return std::ranges::count(placement->cell_types[cell_type], object_type, &PlacedObject::type);
  };
  // Every road cell: 20 cacti, 20 bushes, 40 stones; station cells: a 45-cactus fence, 40 stones,
  // 20 bushes and two station signs.
  EXPECT_EQ(placement->cell_types[0].size(), 80U);
  EXPECT_EQ(count(1, 1), 20);
  EXPECT_EQ(count(1, 7), 20);
  EXPECT_EQ(placement->cell_types[11].size(), 107U);
  EXPECT_EQ(count(11, 1), 45);
  EXPECT_EQ(count(11, 3), 40);
  EXPECT_EQ(count(12, 11), 2);
  EXPECT_EQ(count(7, 9), 3);  // a junction's signs
  const auto& first = placement->cell_types[0][0];
  EXPECT_EQ(first.x, 0x1380);
  EXPECT_EQ(first.y, 0x3c80);
  EXPECT_EQ(first.type, 1);
}

}  // namespace
}  // namespace hp2::host
