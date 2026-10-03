#include "core/screen.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "core/image_view.hpp"

namespace hp2 {
namespace {

TEST(Screen, SplitsIntoTwoViewports) {
  auto screen = Screen{};
  EXPECT_EQ(screen.Rows(Viewport::kUpper), Screen::kHeight);
  EXPECT_EQ(screen.Rows(Viewport::kLower), 0);
  screen.EnableSplit(132);
  EXPECT_EQ(screen.Rows(Viewport::kUpper), 132);
  EXPECT_EQ(screen.FirstRow(Viewport::kLower), 132);
  EXPECT_EQ(screen.Rows(Viewport::kLower), 68);
  screen.EnableSplit(0);
  EXPECT_EQ(screen.SplitRow(), 0);
  screen.EnableSplit(Screen::kHeight);
  EXPECT_EQ(screen.SplitRow(), 0);
}

TEST(Screen, DrawsInViewportCoordinates) {
  auto screen = Screen{};
  screen.EnableSplit(100);
  screen.SetViewport(Viewport::kUpper);
  screen.Clear(1);
  screen.SetViewport(Viewport::kLower);
  screen.Clear(2);
  EXPECT_EQ(screen.ScreenRow(99)[0], 1);
  EXPECT_EQ(screen.ScreenRow(100)[0], 2);

  // A 2x2 image at the lower viewport's row 0 lands on screen row 100, offset by 16.
  const auto pixels = std::to_array<std::uint8_t>({3, 4, 5, 6});
  screen.Blit(ImageView{.width = 2, .height = 2, .pixels = pixels}, Point{.x = 318, .y = -1}, 16);
  EXPECT_EQ(screen.ScreenRow(100)[318], 21);
  EXPECT_EQ(screen.ScreenRow(100)[319], 22);
  EXPECT_EQ(screen.ScreenRow(99)[318], 1);  // the row above belongs to the upper viewport: clipped
}

TEST(Screen, MaskedBlitKeepsIndexZero) {
  auto screen = Screen{};
  screen.Clear(9);
  const auto pixels = std::to_array<std::uint8_t>({0, 4, 5, 0});
  screen.BlitMasked(ImageView{.width = 2, .height = 2, .pixels = pixels}, Point{.x = 1, .y = 1}, 16);
  EXPECT_EQ(screen.ScreenRow(1)[1], 9);
  EXPECT_EQ(screen.ScreenRow(1)[2], 20);
  EXPECT_EQ(screen.ScreenRow(2)[1], 21);
  EXPECT_EQ(screen.ScreenRow(2)[2], 9);
}

TEST(Screen, CopiesOverlappingAreas) {
  auto screen = Screen{};
  screen.EnableSplit(10);
  screen.SetViewport(Viewport::kLower);
  for (auto row = std::uint16_t{0}; row < 4; ++row) {
    screen.Row(row)[0] = static_cast<std::uint8_t>(row + 1);
    screen.Row(row)[1] = static_cast<std::uint8_t>(row + 11);
  }
  // Down one row and right one column, over itself.
  screen.Copy(Point{.x = 0, .y = 0}, Point{.x = 1, .y = 1}, 2, 4);
  EXPECT_EQ(screen.ScreenRow(11)[1], 1);
  EXPECT_EQ(screen.ScreenRow(11)[2], 11);
  EXPECT_EQ(screen.ScreenRow(14)[1], 4);
  EXPECT_EQ(screen.ScreenRow(14)[2], 14);
  // Parts outside the viewport are skipped: nothing reaches the upper viewport's last row.
  screen.Copy(Point{.x = 0, .y = 0}, Point{.x = 0, .y = -1}, 2, 2);
  EXPECT_EQ(screen.ScreenRow(9)[0], 0);
  EXPECT_EQ(screen.ScreenRow(10)[0], 2);
}

TEST(Screen, XorsAcrossRowsAndViewports) {
  auto screen = Screen{};
  screen.EnableSplit(1);
  const auto masks = std::to_array<std::uint8_t>({1, 2, 4});
  screen.XorScreen(Screen::kWidth - 1, masks);
  EXPECT_EQ(screen.ScreenRow(0)[Screen::kWidth - 1], 1);
  EXPECT_EQ(screen.ScreenRow(1)[0], 2);
  EXPECT_EQ(screen.ScreenRow(1)[1], 4);
  screen.XorScreen(Screen::kWidth - 1, masks);
  EXPECT_EQ(screen.ScreenRow(1)[0], 0);
  screen.XorScreen((std::uint32_t{Screen::kWidth} * Screen::kHeight) - 1, masks);  // the rest is past the end
  EXPECT_EQ(screen.ScreenRow(Screen::kHeight - 1)[Screen::kWidth - 1], 1);
}

TEST(Screen, SelectingLowerWithoutSplitIsIgnored) {
  auto screen = Screen{};
  screen.SetViewport(Viewport::kLower);
  EXPECT_EQ(screen.SelectedViewport(), Viewport::kUpper);
}

TEST(ScreenPalette, OverlaysAtAnOffset) {
  auto palette = ScreenPalette{};
  palette.ClearDirty();
  const auto colors = std::to_array<Rgb>({FromAmiga(0x0f00), FromAmiga(0x00f0)});
  palette.Overlay(colors, 254);
  EXPECT_TRUE(palette.Dirty());
  EXPECT_EQ(palette.Color(254), (Rgb{.red = 0xff, .green = 0, .blue = 0}));
  EXPECT_EQ(palette.Color(255), (Rgb{.red = 0, .green = 0xff, .blue = 0}));
  palette.Overlay(colors, 256);  // nothing past the last entry
  EXPECT_EQ(palette.Color(0), Rgb{});
}

}  // namespace
}  // namespace hp2
