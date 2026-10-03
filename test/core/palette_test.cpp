#include "core/palette.hpp"

#include <gtest/gtest.h>

namespace hp2 {
namespace {

TEST(Palette, ConvertsAtariStColours) {
  // PaletteSTToAmiga's gun table: 0,2,4,6,8,a,c,f.
  EXPECT_EQ(FadedColor(0x257, ColorFormat::kAtariSt), FromAmiga(0x4af));
  EXPECT_EQ(FadedColor(0x777, ColorFormat::kAtariSt), FromAmiga(0xfff));
  EXPECT_EQ(FadedColor(0x000, ColorFormat::kAtariSt), FromAmiga(0x000));
}

TEST(Palette, FadesInTheStoredFormat) {
  // FadePaletteList subtracts the level from each stored gun, floor 0, before any conversion.
  EXPECT_EQ(FadedColor(0x257, ColorFormat::kAtariSt, 1), FromAmiga(0x28c));  // ST 146
  EXPECT_EQ(FadedColor(0x777, ColorFormat::kAtariSt, 7), FromAmiga(0x000));
  EXPECT_EQ(FadedColor(0xfa3, ColorFormat::kAmiga, 4), FromAmiga(0xb60));
  EXPECT_EQ(FadedColor(0xfff, ColorFormat::kAmiga, 7), FromAmiga(0x888));  // Amiga colours never reach black
}

TEST(Palette, OverlaysASegmentAtItsRegisters) {
  const auto segment =
      PaletteSegment{.first_register = 2, .count = 2, .format = ColorFormat::kAtariSt, .colors = {0x700, 0x070}};
  auto palette = ScreenPalette{};
  palette.ClearDirty();
  palette.Overlay(segment, 0, 16);
  EXPECT_TRUE(palette.Dirty());
  EXPECT_EQ(palette.Color(18), FromAmiga(0xf00));
  EXPECT_EQ(palette.Color(19), FromAmiga(0x0f0));
  EXPECT_EQ(palette.Color(20), Rgb{});
  palette.Overlay(segment, 6);
  EXPECT_EQ(palette.Color(2), FromAmiga(0x200));
  EXPECT_EQ(palette.Color(3), FromAmiga(0x020));
}

}  // namespace
}  // namespace hp2
