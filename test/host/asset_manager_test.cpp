#include "host/asset_manager.hpp"

#include <gtest/gtest.h>

#include "assets.hpp"
#include "core/engine_assets.hpp"

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

}  // namespace
}  // namespace hp2::host
