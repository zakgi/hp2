#include "core/office.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>

#include "assets.hpp"
#include "core/game_state.hpp"
#include "core/key_events.hpp"
#include "core/screen.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class OfficeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
    office_.OnEnter();
    EXPECT_EQ(office_.Step(Office::kLightsSeconds), ComponentType::kOffice);
  }

  void Press(Key key) { keys_.Record({.key = key, .action = KeyAction::kPress}); }
  void Release(Key key) { keys_.Record({.key = key, .action = KeyAction::kRelease}); }

  // Holds an arrow for as long as it takes to bring the pointer to `target`, landing half a pixel
  // past it so that the pointer reads exactly `target`.
  void MoveTo(Point target) {
    const auto travel = [this](float distance, Key forward, Key backward) {
      const auto key = distance >= 0.0F ? forward : backward;
      Press(key);
      EXPECT_EQ(office_.Step(std::abs(distance) / Office::kPointerSpeed), ComponentType::kOffice);
      Release(key);
    };
    travel(static_cast<float>(target.x) + 0.5F - pointer_x_, Key::kRight, Key::kLeft);
    travel(static_cast<float>(target.y) + 0.5F - pointer_y_, Key::kDown, Key::kUp);
    pointer_x_ = static_cast<float>(target.x) + 0.5F;
    pointer_y_ = static_cast<float>(target.y) + 0.5F;
    ASSERT_EQ(office_.Pointer().x, target.x);
    ASSERT_EQ(office_.Pointer().y, target.y);
  }

  void Click() {
    Press(Key::kSpace);
    EXPECT_EQ(office_.Step(0.0F), ComponentType::kOffice);
    Release(Key::kSpace);
    // Long enough for any slide to finish.
    EXPECT_EQ(office_.Step(1.0F), ComponentType::kOffice);
  }

  // A point of the desk in the color of `drawer`, away from the drawer's edges.
  [[nodiscard]] std::optional<Point> DrawerPoint(std::size_t drawer) const {
    const auto& picture = manager_.Engine().Picture(EnginePicture::kOffice);
    const auto color = Office::kDrawerColors[drawer];
    const auto pixel = [&picture](int column, int row) {
      return picture.Row(static_cast<std::uint16_t>(row))[static_cast<std::size_t>(column)];
    };
    auto found = std::optional<Point>{};
    for (auto row = Office::kDesk.top + 2; not found and row < Office::kDesk.bottom - 1; ++row) {
      for (auto column = Office::kDesk.left + 2; not found and column < Office::kDesk.right - 1; ++column) {
        auto inside = true;
        for (auto row_step = -2; row_step <= 2; ++row_step) {
          for (auto column_step = -2; column_step <= 2; ++column_step) {
            inside = inside and pixel(column + column_step, row + row_step) == color;
          }
        }
        if (inside) {
          found = Point{.x = static_cast<std::int16_t>(column), .y = static_cast<std::int16_t>(row)};
        }
      }
    }
    return found;
  }

  [[nodiscard]] static Point Middle(const Office::Area& area) {
    return Point{.x = static_cast<std::int16_t>((area.left + area.right) / 2),
                 .y = static_cast<std::int16_t>((area.top + area.bottom) / 2)};
  }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  GameState game_;
  Office office_{manager_.Engine(), screen_, keys_, game_};
  float pointer_x_{Office::kPointerStart.x};
  float pointer_y_{Office::kPointerStart.y};
};

TEST_F(OfficeTest, TurnsTheLightsOn) {
  const auto colors = manager_.Engine().Palette(EnginePalette::kOffice);
  EXPECT_EQ(screen_.Palette(Viewport::kUpper).Color(15), colors[15]);
  EXPECT_FALSE(office_.Poster().has_value());
}

TEST_F(OfficeTest, OpensAPosterAndTakesItsMission) {
  const auto drawer = DrawerPoint(0);
  ASSERT_TRUE(drawer.has_value());
  MoveTo(*drawer);
  Click();
  ASSERT_EQ(office_.Poster(), 0U);
  MoveTo(Middle(Office::kPosterAreas[0]));
  Press(Key::kEnter);
  EXPECT_EQ(office_.Step(0.0F), ComponentType::kOffice);
  EXPECT_EQ(office_.Step(Office::kLightsSeconds), ComponentType::kHighway);
  EXPECT_EQ(game_.mission, 0U);
  EXPECT_FALSE(game_.missions_available[0]);
  EXPECT_EQ(game_.MissionsLeft(), 5U);
}

TEST_F(OfficeTest, TheSameDrawerBringsOutItsOtherPoster) {
  const auto drawer = DrawerPoint(2);
  ASSERT_TRUE(drawer.has_value());
  MoveTo(*drawer);
  Click();
  EXPECT_EQ(office_.Poster(), 4U);
  Click();
  EXPECT_EQ(office_.Poster(), 5U);
}

TEST_F(OfficeTest, TakenPostersStayInTheDrawer) {
  game_.missions_available[2] = false;
  const auto drawer = DrawerPoint(1);
  ASSERT_TRUE(drawer.has_value());
  MoveTo(*drawer);
  Click();
  EXPECT_EQ(office_.Poster(), 3U);
  game_.missions_available[3] = false;
  Click();
  EXPECT_FALSE(office_.Poster().has_value());
}

TEST_F(OfficeTest, ClicksOutsideTheDeskDoNothing) {
  MoveTo(Point{.x = 250, .y = 100});
  Click();
  EXPECT_FALSE(office_.Poster().has_value());
  Press(Key::kEscape);
  EXPECT_EQ(office_.Step(0.0F), ComponentType::kQuit);
}

}  // namespace
}  // namespace hp2
