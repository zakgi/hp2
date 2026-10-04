#include "core/presentation.hpp"

#include <gtest/gtest.h>

#include "core/palette.hpp"
#include "core/screen.hpp"

namespace hp2 {
namespace {

constexpr auto kWhite = Rgb{.red = 0xff, .green = 0xff, .blue = 0xff};

TEST(Fade, MovesThePaletteEasedToItsEnd) {
  auto screen = Screen{};
  auto& palette = screen.Palette(Viewport::kUpper);
  palette.SetColor(1, kWhite);
  auto fade = Fade{screen, 0.0F, 1.0F, 0.5F};
  EXPECT_EQ(palette.Color(1), Rgb{});  // black as soon as it is made
  fade.Init();
  EXPECT_FALSE(fade.Tick(0.25F));
  // Halfway in time is halfway in brightness; the easing is symmetric.
  EXPECT_EQ(palette.Color(1), (Rgb{.red = 0x80, .green = 0x80, .blue = 0x80}));
  EXPECT_TRUE(fade.Tick(0.5F));
  EXPECT_EQ(palette.Color(1), kWhite);
}

TEST(Fade, FadesOut) {
  auto screen = Screen{};
  auto& palette = screen.Palette(Viewport::kUpper);
  palette.SetColor(1, kWhite);
  auto fade = Fade{screen, 1.0F, 0.0F, 0.5F};
  fade.Init();
  EXPECT_FALSE(fade.Tick(0.1F));
  EXPECT_LT(palette.Color(1).red, 0xff);
  EXPECT_GT(palette.Color(1).red, 0xcc);  // eased: slower than linear at the start
  EXPECT_TRUE(fade.Tick(0.4F));
  EXPECT_EQ(palette.Color(1), Rgb{});
}

}  // namespace
}  // namespace hp2
