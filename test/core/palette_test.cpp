#include "core/palette.hpp"

#include <gtest/gtest.h>

namespace hp2 {
namespace {

TEST(Palette, ScalesColorsByBrightness) {
  const auto color = Rgb{.red = 0xff, .green = 0x80, .blue = 0x01};
  EXPECT_EQ(ScaleColor(color, 1.0F), color);
  EXPECT_EQ(ScaleColor(color, 0.5F), (Rgb{.red = 0x80, .green = 0x40, .blue = 0x01}));
  EXPECT_EQ(ScaleColor(color, 0.0F), Rgb{});
}

TEST(Palette, OverlaysASegmentAtItsRegisters) {
  const auto red = Rgb{.red = 0xff, .green = 0x00, .blue = 0x00};
  const auto green = Rgb{.red = 0x00, .green = 0xff, .blue = 0x00};
  const auto segment = PaletteSegment{.first_register = 2, .count = 2, .colors = {red, green}};
  auto palette = ScreenPalette{};
  palette.Overlay(segment, 16);
  EXPECT_EQ(palette.Color(18), red);
  EXPECT_EQ(palette.Color(19), green);
  EXPECT_EQ(palette.Color(20), Rgb{});
  palette.Overlay(segment);
  EXPECT_EQ(palette.Color(2), red);
  EXPECT_EQ(palette.Color(3), green);
}

}  // namespace
}  // namespace hp2
