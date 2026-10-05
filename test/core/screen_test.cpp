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

TEST(Screen, DrawsClippedVerticalLines) {
  auto screen = Screen{};
  screen.Clear(0);
  screen.DrawVerticalLine(-5, 2, 3, 7);
  EXPECT_EQ(screen.ScreenRow(0)[3], 7);
  EXPECT_EQ(screen.ScreenRow(2)[3], 7);
  EXPECT_EQ(screen.ScreenRow(3)[3], 0);
  EXPECT_EQ(screen.ScreenRow(0)[4], 0);
  screen.DrawVerticalLine(198, 250, Screen::kWidth, 7);  // right of the screen: nothing
  EXPECT_EQ(screen.ScreenRow(199)[Screen::kWidth - 1], 0);
}

TEST(Screen, DrawsLinesWithBothEnds) {
  auto screen = Screen{};
  screen.Clear(0);
  // A steep line: the rows are counted, the column follows.
  screen.DrawLine(Point{.x = 10, .y = 10}, Point{.x = 11, .y = 13}, 7);
  EXPECT_EQ(screen.ScreenRow(10)[10], 7);
  EXPECT_EQ(screen.ScreenRow(11)[10], 7);
  EXPECT_EQ(screen.ScreenRow(12)[11], 7);
  EXPECT_EQ(screen.ScreenRow(13)[11], 7);
  EXPECT_EQ(screen.ScreenRow(11)[11], 0);
  EXPECT_EQ(screen.ScreenRow(14)[11], 0);
  // The same pixels from the other end.
  screen.Clear(0);
  screen.DrawLine(Point{.x = 11, .y = 13}, Point{.x = 10, .y = 10}, 7);
  EXPECT_EQ(screen.ScreenRow(11)[10], 7);
  EXPECT_EQ(screen.ScreenRow(12)[11], 7);
  // A diagonal, and a single point.
  screen.DrawLine(Point{.x = 23, .y = 20}, Point{.x = 20, .y = 23}, 5);
  EXPECT_EQ(screen.ScreenRow(20)[23], 5);
  EXPECT_EQ(screen.ScreenRow(21)[22], 5);
  EXPECT_EQ(screen.ScreenRow(23)[20], 5);
  screen.DrawLine(Point{.x = 40, .y = 40}, Point{.x = 40, .y = 40}, 6);
  EXPECT_EQ(screen.ScreenRow(40)[40], 6);
}

TEST(Screen, ClipsLinesToTheViewport) {
  auto screen = Screen{};
  screen.EnableSplit(100);
  screen.DrawLine(Point{.x = -3, .y = 98}, Point{.x = 3, .y = 104}, 7);
  EXPECT_EQ(screen.ScreenRow(98)[0], 0);
  EXPECT_EQ(screen.ScreenRow(99)[0], 0);
  // Left of the screen until its fourth step, then two rows of the upper viewport.
  screen.DrawLine(Point{.x = -3, .y = 95}, Point{.x = 3, .y = 101}, 7);
  EXPECT_EQ(screen.ScreenRow(98)[0], 7);
  EXPECT_EQ(screen.ScreenRow(99)[1], 7);
  EXPECT_EQ(screen.ScreenRow(100)[2], 0);
}

TEST(ScreenPalette, OverlaysAtAnOffset) {
  auto palette = ScreenPalette{};
  const auto colors =
      std::to_array<Rgb>({{.red = 0xff, .green = 0x00, .blue = 0x00}, {.red = 0x00, .green = 0xff, .blue = 0x00}});
  palette.Overlay(colors, 254);
  EXPECT_EQ(palette.Color(254), (Rgb{.red = 0xff, .green = 0, .blue = 0}));
  EXPECT_EQ(palette.Color(255), (Rgb{.red = 0, .green = 0xff, .blue = 0}));
  palette.Overlay(colors, 256);  // nothing past the last entry
  EXPECT_EQ(palette.Color(0), Rgb{});
}

}  // namespace
}  // namespace hp2
