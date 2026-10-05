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

}  // namespace
}  // namespace hp2
