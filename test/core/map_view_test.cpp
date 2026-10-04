#include "core/map_view.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "assets.hpp"
#include "core/palette.hpp"
#include "core/road.hpp"
#include "core/screen.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class MapViewTest : public testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
  }
  [[nodiscard]] std::uint8_t GetPixel(Point point) const {
    return screen_.ScreenRow(static_cast<std::uint16_t>(point.y))[static_cast<std::size_t>(point.x)];
  }
  host::AssetManager manager_;
  Screen screen_;
};

// The middle of `cell`.
WorldPoint Middle(Cell cell) {
  return GetCellOrigin(cell) + WorldPoint{.x = kCellUnits / 2, .y = kCellUnits / 2};
}

TEST_F(MapViewTest, DrawsRoadsDesertAndTheCars) {
  const auto& assets = manager_.Engine();
  const auto road = Road{assets.road_map, assets.road_shapes, assets.scenery};
  const auto player = Middle(Cell{.x = 20, .y = 20});
  const auto target = Middle(Cell{.x = 30, .y = 30});
  auto map_view = MapView{screen_, road};
  map_view.Draw(player, target);

  // The north-south straight at (13, 2): road down its middle column, desert beside it.
  const auto straight = GetMapPixel(Middle(Cell{.x = 13, .y = 2}));
  EXPECT_EQ(straight.x, 127);
  EXPECT_EQ(straight.y, 187);
  for (auto row = std::int16_t{185}; row <= 189; ++row) {
    EXPECT_EQ(GetPixel(Point{.x = 127, .y = row}), kMapRoad) << "row " << row;
  }
  EXPECT_EQ(GetPixel(Point{.x = 126, .y = 187}), kMapDesert);
  // The middle of the desert cell (0, 0), and the margin left of the map.
  EXPECT_EQ(GetPixel(Point{.x = 62, .y = 197}), kMapDesert);
  EXPECT_EQ(GetPixel(Point{.x = 10, .y = 100}), kMapBlack);
  EXPECT_EQ(GetPixel(GetMapPixel(player)), kMapPlayer);
  EXPECT_EQ(GetPixel(GetMapPixel(target)), kMapTarget);
  EXPECT_EQ(screen_.Palette(Viewport::kUpper).Color(kMapTarget), (Rgb{.red = 0xff, .green = 0x00, .blue = 0x00}));
}

}  // namespace
}  // namespace hp2
