#include "core/office.hpp"

#include "core/engine_state.hpp"

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
    state_.emplace(manager_.Engine(), screen_, keys_, audio_, 42);
    office_.OnEnter(*state_);
    EXPECT_EQ(office_.Step(*state_, Office::kLightsSeconds), ComponentType::Office);
  }

  void Press(Key key) { keys_.Record({.key = key, .action = KeyAction::Press}); }
  void Release(Key key) { keys_.Record({.key = key, .action = KeyAction::Release}); }

  // Holds an arrow for as long as it takes to bring the pointer to `target`, landing half a pixel
  // past it so that the pointer reads exactly `target`.
  void MoveTo(Point target) {
    const auto travel = [this](float distance, Key forward, Key backward) {
      const auto key = distance >= 0.0F ? forward : backward;
      Press(key);
      EXPECT_EQ(office_.Step(*state_, std::abs(distance) / Office::kPointerSpeed), ComponentType::Office);
      Release(key);
    };
    travel(static_cast<float>(target.x) + 0.5F - pointer_x_, Key::Right, Key::Left);
    travel(static_cast<float>(target.y) + 0.5F - pointer_y_, Key::Down, Key::Up);
    pointer_x_ = static_cast<float>(target.x) + 0.5F;
    pointer_y_ = static_cast<float>(target.y) + 0.5F;
    ASSERT_EQ(Office::Pointer(*state_).x, target.x);
    ASSERT_EQ(Office::Pointer(*state_).y, target.y);
  }

  void Click() {
    Press(Key::Space);
    EXPECT_EQ(office_.Step(*state_, 0.0F), ComponentType::Office);
    Release(Key::Space);
    // Long enough for any slide to finish.
    EXPECT_EQ(office_.Step(*state_, 1.0F), ComponentType::Office);
  }

  // A point of the desk in the color of `drawer`, away from the drawer's edges.
  [[nodiscard]] std::optional<Point> DrawerPoint(std::size_t drawer) const {
    const auto& picture = manager_.Engine().Picture(EnginePicture::Office);
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
  AudioEngine audio_;
  std::optional<EngineState> state_;
  Office office_;
  float pointer_x_{Office::kPointerStart.x};
  float pointer_y_{Office::kPointerStart.y};
};

TEST_F(OfficeTest, TurnsTheLightsOn) {
  const auto colors = manager_.Engine().Palette(EnginePalette::Office);
  EXPECT_EQ(screen_.Palette(Viewport::Upper).Color(15), colors[15]);
  EXPECT_FALSE(Office::Poster(*state_).has_value());
}

TEST_F(OfficeTest, OpensAPosterAndTakesItsMission) {
  const auto drawer = DrawerPoint(0);
  ASSERT_TRUE(drawer.has_value());
  MoveTo(*drawer);
  Click();
  ASSERT_EQ(Office::Poster(*state_), 0U);
  MoveTo(Middle(Office::kPosterAreas[0]));
  Press(Key::Enter);
  EXPECT_EQ(office_.Step(*state_, 0.0F), ComponentType::Office);
  EXPECT_EQ(office_.Step(*state_, Office::kLightsSeconds), ComponentType::Highway);
  EXPECT_EQ(state_->game.mission, 0U);
  EXPECT_FALSE(state_->game.missions_available[0]);
  EXPECT_EQ(state_->game.MissionsLeft(), 5U);
}

TEST_F(OfficeTest, TheSameDrawerBringsOutItsOtherPoster) {
  const auto drawer = DrawerPoint(2);
  ASSERT_TRUE(drawer.has_value());
  MoveTo(*drawer);
  Click();
  EXPECT_EQ(Office::Poster(*state_), 4U);
  Click();
  EXPECT_EQ(Office::Poster(*state_), 5U);
}

TEST_F(OfficeTest, TakenPostersStayInTheDrawer) {
  state_->game.missions_available[2] = false;
  const auto drawer = DrawerPoint(1);
  ASSERT_TRUE(drawer.has_value());
  MoveTo(*drawer);
  Click();
  EXPECT_EQ(Office::Poster(*state_), 3U);
  state_->game.missions_available[3] = false;
  Click();
  EXPECT_FALSE(Office::Poster(*state_).has_value());
}

TEST_F(OfficeTest, ClicksOutsideTheDeskDoNothing) {
  MoveTo(Point{.x = 250, .y = 100});
  Click();
  EXPECT_FALSE(Office::Poster(*state_).has_value());
  Press(Key::Escape);
  EXPECT_EQ(office_.Step(*state_, 0.0F), ComponentType::Quit);
}

}  // namespace
}  // namespace hp2
