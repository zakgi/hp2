#include "core/palette.hpp"

#include <gtest/gtest.h>

namespace hp2 {
namespace {

TEST(Palette, FadesEvenlyToBlack) {
  const auto color = Rgb{.red = 0xff, .green = 0x70, .blue = 0x07};
  EXPECT_EQ(FadedColor(color, 0), color);
  // Level 3 of 7 leaves 4 sevenths of each channel.
  EXPECT_EQ(FadedColor(color, 3), (Rgb{.red = 0x91, .green = 0x40, .blue = 0x04}));
  EXPECT_EQ(FadedColor(color, kFadeLevels - 1), Rgb{});
}

TEST(Palette, OverlaysASegmentAtItsRegisters) {
  const auto red = Rgb{.red = 0xff, .green = 0x00, .blue = 0x00};
  const auto green = Rgb{.red = 0x00, .green = 0xff, .blue = 0x00};
  const auto segment = PaletteSegment{.first_register = 2, .count = 2, .colors = {red, green}};
  auto palette = ScreenPalette{};
  palette.Overlay(segment, 0, 16);
  EXPECT_EQ(palette.Color(18), red);
  EXPECT_EQ(palette.Color(19), green);
  EXPECT_EQ(palette.Color(20), Rgb{});
  palette.Overlay(segment, 6);
  EXPECT_EQ(palette.Color(2), FadedColor(red, 6));
  EXPECT_EQ(palette.Color(3), FadedColor(green, 6));
}

}  // namespace
}  // namespace hp2
