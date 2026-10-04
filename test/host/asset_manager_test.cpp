#include "host/asset_manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>

#include "assets.hpp"
#include "core/engine_assets.hpp"
#include "core/road_map.hpp"

namespace hp2::host {
namespace {

TEST(AssetManager, LoadsEveryAsset) {
  if (not test::Executable()) {
    GTEST_SKIP() << "game disk not present at " << test::DiskImage();
  }
  auto manager = AssetManager{};
  ASSERT_TRUE(manager.Load(test::DiskImage()));
  const auto& assets = manager.Engine();
  for (const auto& picture : assets.pictures) {
    EXPECT_EQ(picture.pixels.size(), 64000U);
  }
  EXPECT_EQ(assets.Palette(EnginePalette::kTitle).size(), 2U);
  EXPECT_EQ(assets.Palette(EnginePalette::kView).size(), 39U);
  ASSERT_EQ(assets.Palette(EnginePalette::kLogo).size(), 1U);
  EXPECT_EQ(assets.Palette(EnginePalette::kLogo)[0].count, 16);
  for (const auto& palette : assets.palettes) {
    EXPECT_FALSE(palette.empty());
  }
  const auto& names = assets.Bank(EngineBank::kNames);
  ASSERT_EQ(names.sprites.size(), 3U);
  EXPECT_EQ(names.GetImage(2).width, 48);
  EXPECT_EQ(names.GetImage(2).height, 53);
  EXPECT_EQ(assets.Bank(EngineBank::kCar1).sprites.size(), 10U);
  EXPECT_EQ(assets.Bank(EngineBank::kScore).sprites.size(), 12U);
  EXPECT_EQ(assets.Font(EngineFont::kLettre1).GlyphCount(), 94U);
  EXPECT_EQ(assets.Font(EngineFont::kLettre2).GetGlyph('A').pixels.size(), 64U);
  EXPECT_EQ(assets.Sound(EngineSound::kEngine).rate_hz, 6'628U);
  EXPECT_EQ(assets.Sound(EngineSound::kEngine).loop_length, 0x254U);
  EXPECT_EQ(assets.Sound(EngineSound::kSiren).rate_hz, 4'143U);
  EXPECT_EQ(assets.Sound(EngineSound::kSiren).loop_length, assets.Sound(EngineSound::kSiren).samples.size());
  EXPECT_EQ(assets.Sound(EngineSound::kShot).loop_length, 0U);
  EXPECT_EQ(assets.title_animation.frames.size(), 19U);
  EXPECT_EQ(assets.title_animation.steps.size(), 27U);
  EXPECT_EQ(assets.title_animation.frames[0].run_count, 54U);
  EXPECT_EQ(assets.title_animation.frames[0].source_words, 206U);
  EXPECT_EQ(assets.title_music.samples.size(), 15U);
  EXPECT_EQ(assets.road_map.cells.size(), 4096U);
  EXPECT_EQ(assets.scenery.GetObjects(11).size(), 107U);
}

TEST(AssetManager, ReadsTheRoadShapes) {
  if (not test::Executable()) {
    GTEST_SKIP() << "game disk not present at " << test::DiskImage();
  }
  auto manager = AssetManager{};
  ASSERT_TRUE(manager.Load(test::DiskImage()));
  const auto& shapes = manager.Engine().road_shapes;
  // Points per road cell type, the closing point included: none, straights, quarter circles, T
  // junctions, crossroads, stations.
  const auto counts = std::to_array<std::size_t>({0, 7, 7, 53, 53, 53, 53, 11, 11, 11, 17, 15, 15});
  for (auto type = std::size_t{0}; type < kRoadCellTypeCount; ++type) {
    EXPECT_EQ(shapes.GetOutline(type).size(), counts[type]) << "cell type " << type;
  }
  EXPECT_EQ(shapes.GetOutline(1).front(), (ShapePoint{.x = 8704, .y = 0}));
  // The station's driveway loop joins the road through a one-unit slit.
  const auto station = shapes.GetOutline(11);
  EXPECT_NE(std::ranges::find(station, ShapePoint{.x = 8705, .y = 14336}), station.end());
  EXPECT_NE(std::ranges::find(station, ShapePoint{.x = 8705, .y = 13056}), station.end());
}

}  // namespace
}  // namespace hp2::host
