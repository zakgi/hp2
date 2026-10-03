#include "host/asset_manager.hpp"

#include <gtest/gtest.h>

#include "assets.hpp"

namespace hp2::host {
namespace {

TEST(AssetManager, FindsFilesWhateverTheCase) {
  const auto directory = FindFile(test::GameDir(), "disk2_2");
  if (not directory) {
    GTEST_SKIP() << "game files not present in " << test::GameDir();
  }
  // The disk has Sirene.snd and bureau.cpv; the program asks for SIRENE.SND and BUREAU.CPV.
  EXPECT_TRUE(FindFile(*directory, "SIRENE.SND").has_value());
  EXPECT_TRUE(FindFile(*directory, "BUREAU.CPV").has_value());
  EXPECT_FALSE(FindFile(*directory, "MISSING.BIN").has_value());
}

TEST(AssetManager, LoadsThePresentation) {
  if (not test::Executable()) {
    GTEST_SKIP() << "game files not present in " << test::GameDir();
  }
  auto manager = AssetManager{};
  ASSERT_TRUE(manager.Load(test::GameDir()));
  const auto& assets = manager.Engine();
  EXPECT_EQ(assets.title_picture.width, 320);
  EXPECT_EQ(assets.title_picture.height, 200);
  EXPECT_EQ(assets.title_picture.pixels.size(), 64000U);
  EXPECT_EQ(assets.title_palette.size(), 2U);
  EXPECT_EQ(assets.logo_picture.pixels.size(), 64000U);
  ASSERT_EQ(assets.logo_palette.size(), 1U);
  EXPECT_EQ(assets.logo_palette[0].count, 16);
  EXPECT_EQ(assets.title_animation.frames.size(), 19U);
  EXPECT_EQ(assets.title_animation.steps.size(), 27U);
  EXPECT_EQ(assets.title_animation.frames[0].runs.size(), 54U);
  EXPECT_EQ(assets.title_animation.frames[0].source_words, 206U);
  ASSERT_EQ(assets.name_images.size(), 3U);
  EXPECT_EQ(assets.name_images[2].width, 48);
  EXPECT_EQ(assets.name_images[2].height, 53);
}

}  // namespace
}  // namespace hp2::host
