#include "core/driver_view.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "assets.hpp"
#include "core/ai.hpp"
#include "core/cockpit.hpp"
#include "core/engine_assets.hpp"
#include "core/game_state.hpp"
#include "core/mission.hpp"
#include "core/palette.hpp"
#include "core/road.hpp"
#include "core/road_projection.hpp"
#include "core/roadside_objects.hpp"
#include "core/screen.hpp"
#include "core/sky_horizon.hpp"
#include "core/view_palette.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

constexpr auto kNorth = kFullTurn / 4.0F;
constexpr auto kViewRows = std::size_t{kDashboardRow};
using ViewPixels = std::array<std::uint8_t, kViewRows * Screen::kWidth>;

class DriverViewTest : public testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
  }
  [[nodiscard]] Road MakeRoad() const {
    const auto& assets = manager_.Engine();
    return Road{assets.road_map, assets.road_shapes, assets.scenery};
  }
  [[nodiscard]] std::uint8_t GetPixel(std::int16_t column, std::int16_t row) const {
    return screen_.ScreenRow(static_cast<std::uint16_t>(row))[static_cast<std::size_t>(column)];
  }
  // The rows of the view, above the dashboard.
  [[nodiscard]] ViewPixels GetViewPixels() const {
    auto pixels = ViewPixels{};
    for (auto row = std::size_t{0}; row < kViewRows; ++row) {
      std::ranges::copy(screen_.ScreenRow(static_cast<std::uint16_t>(row)),
                        pixels.begin() + static_cast<std::ptrdiff_t>(row * Screen::kWidth));
    }
    return pixels;
  }
  // The whole screen, view and dashboard.
  [[nodiscard]] std::array<std::uint8_t, std::size_t{Screen::kHeight} * Screen::kWidth> GetScreenPixels() const {
    auto pixels = std::array<std::uint8_t, std::size_t{Screen::kHeight} * Screen::kWidth>{};
    for (auto row = std::size_t{0}; row < Screen::kHeight; ++row) {
      std::ranges::copy(screen_.ScreenRow(static_cast<std::uint16_t>(row)),
                        pixels.begin() + static_cast<std::ptrdiff_t>(row * Screen::kWidth));
    }
    return pixels;
  }
  host::AssetManager manager_;
  Screen screen_;
};

TEST_F(DriverViewTest, SplitsTheScreenAndPlacesTheColors) {
  const auto& assets = manager_.Engine();
  auto mission = Mission{MakeRoad()};
  mission.Start(kMissions[0], AiPolicies{}, 42);
  auto view = DriverView{assets, screen_, mission.GetRoad()};
  view.Draw(mission);

  EXPECT_EQ(screen_.SplitRow(), kDashboardRow);
  const auto& upper = screen_.Palette(Viewport::kUpper);
  // The sky at the top of the screen, the lightest shade at the horizon, the sand and the asphalt
  // by the car (viewPalette, 0:a520).
  EXPECT_EQ(upper.Color(GetSkyEntry(0)), (Rgb{.red = 0x00, .green = 0x55, .blue = 0xff}));
  EXPECT_EQ(upper.Color(GetSkyEntry(69)), (Rgb{.red = 0xff, .green = 0xff, .blue = 0xff}));
  EXPECT_EQ(upper.Color(GetGroundOffset(131) + kSandColor), (Rgb{.red = 0xaa, .green = 0x66, .blue = 0x33}));
  EXPECT_EQ(upper.Color(GetGroundOffset(131) + kAsphaltColor), (Rgb{.red = 0x55, .green = 0x55, .blue = 0x55}));
  EXPECT_EQ(upper.Color(GetGroundOffset(70) + kSandColor), (Rgb{.red = 0xff, .green = 0xff, .blue = 0xff}));

  // The car starts on the road: sky under the roof strip, asphalt under the eye.
  EXPECT_EQ(GetPixel(160, 20), GetSkyEntry(20));
  EXPECT_EQ(GetPixel(160, 128), GetGroundOffset(128) + kAsphaltColor);
}

TEST_F(DriverViewTest, DrawsTheCarAroundTheView) {
  const auto& assets = manager_.Engine();
  auto mission = Mission{MakeRoad()};
  mission.Start(kMissions[0], AiPolicies{}, 42);
  auto view = DriverView{assets, screen_, mission.GetRoad()};
  view.Draw(mission);

  // The roof strip, in its own colors.
  for (auto row = std::int16_t{0}; row < 16; ++row) {
    for (auto column = std::int16_t{0}; column < Screen::kWidth; ++column) {
      const auto pixel = GetPixel(column, row);
      ASSERT_GE(pixel, kRoofOffset) << "row " << row << " column " << column;
      ASSERT_LT(pixel, kRoofOffset + kColorRegisterCount) << "row " << row << " column " << column;
    }
  }
  // On it the cell the car is in, column then row, two digits each from columns 40 and 64 on row 4.
  const auto cell = GetCell(mission.GetPlayer().position);
  const auto& font = assets.Font(EngineFont::kLettre2);
  const auto expect_digit = [this, &font](int digit, std::int16_t first_column) {
    const auto glyph = font.GetGlyph(static_cast<char>('0' + digit));
    for (auto row = std::uint16_t{0}; row < BitmapFont::kGlyphSize; ++row) {
      for (auto column = std::uint16_t{0}; column < BitmapFont::kGlyphSize; ++column) {
        ASSERT_EQ(GetPixel(static_cast<std::int16_t>(first_column + column), static_cast<std::int16_t>(4 + row)),
                  glyph.Row(row)[column] + kRoofOffset)
            << "digit " << digit << " at column " << first_column;
      }
    }
  };
  ASSERT_GE(cell.x, 10);
  expect_digit(cell.x / 10, 40);
  expect_digit(cell.x % 10, 48);
  expect_digit(cell.y / 10, 64);
  expect_digit(cell.y % 10, 72);
  // The hood's edge: black from a column on, on the three rows above the dashboard.
  EXPECT_NE(GetPixel(247, 129), 0);
  EXPECT_EQ(GetPixel(248, 129), 0);
  EXPECT_NE(GetPixel(171, 130), 0);
  EXPECT_EQ(GetPixel(172, 130), 0);
  EXPECT_NE(GetPixel(117, 131), 0);
  EXPECT_EQ(GetPixel(118, 131), 0);
  EXPECT_EQ(GetPixel(319, 131), 0);
  // The dashboard, in its own palette (viewPalette's last set).
  EXPECT_EQ(screen_.Palette(Viewport::kLower).Color(1), (Rgb{.red = 0xff, .green = 0x88, .blue = 0x44}));
  auto dashboard_pixels = 0;
  for (auto row = kDashboardRow; row < Screen::kHeight; ++row) {
    for (const auto pixel : screen_.ScreenRow(row)) {
      dashboard_pixels += pixel != 0 ? 1 : 0;
    }
  }
  EXPECT_GT(dashboard_pixels, 5000);
}

TEST_F(DriverViewTest, TheNeedlesShowTheCar) {
  const auto& cockpit = manager_.Engine().Bank(EngineBank::kCockpit);
  // The needles' red, in the dashboard's rows.
  constexpr auto kNeedle = std::uint8_t{14};
  const auto draw = [this, &cockpit](const DashboardInput& input) {
    screen_.EnableSplit(kDashboardRow);
    screen_.SetViewport(Viewport::kLower);
    DrawDashboard(cockpit, input, screen_);
  };
  const auto get_pixel = [this](std::int16_t column, std::int16_t row) {
    return GetPixel(column, static_cast<std::int16_t>(kDashboardRow + row));
  };
  // Stopped, the speedometer's needle points down to the left of its hub at (131, 47), and the
  // tachometer's of its own at (188, 47).
  draw(DashboardInput{});
  EXPECT_EQ(get_pixel(131, 47), kNeedle);
  EXPECT_EQ(get_pixel(113, 55), kNeedle);
  EXPECT_EQ(get_pixel(188, 47), kNeedle);
  EXPECT_EQ(get_pixel(169, 53), kNeedle);
  // At the car's top speed it has turned 235.7 degrees, down to the right; reversing shows the
  // same. Pointing right the needle starts a pixel further right.
  for (const auto speed : {8000.0F, -8000.0F}) {
    draw(DashboardInput{.speed = speed});
    EXPECT_EQ(get_pixel(132, 47), kNeedle) << "speed " << speed;
    EXPECT_EQ(get_pixel(149, 58), kNeedle) << "speed " << speed;
    EXPECT_NE(get_pixel(113, 55), kNeedle) << "speed " << speed;
  }
  // The engine at 194 of the original's units: 198 - 108 degrees, straight up.
  draw(DashboardInput{.rpm = 194.0F});
  EXPECT_EQ(get_pixel(188, 27), kNeedle);
  // The small gauges, with the hand on their side turned away: empty and full, cold and hot.
  draw(DashboardInput{.steer = 1.0F, .fuel = 0.0F});
  EXPECT_EQ(get_pixel(86, 56), kNeedle);
  draw(DashboardInput{.steer = 1.0F, .fuel = 1.0F});
  EXPECT_EQ(get_pixel(100, 55), kNeedle);
  EXPECT_NE(get_pixel(86, 56), kNeedle);
  draw(DashboardInput{.steer = -1.0F, .temperature = 0.0F});
  EXPECT_EQ(get_pixel(219, 55), kNeedle);
  draw(DashboardInput{.steer = -1.0F, .temperature = 1.0F});
  EXPECT_EQ(get_pixel(233, 54), kNeedle);
}

TEST_F(DriverViewTest, TheRoofStripTellsWhereTheCarsAreAndTheScore) {
  const auto& assets = manager_.Engine();
  const auto& player_font = assets.Font(EngineFont::kLettre2);
  const auto& target_font = assets.Font(EngineFont::kLettre1);
  const auto text = RoofText{.player = Cell{.x = 11, .y = 15},
                             .player_heading = kNorth,
                             .target = Cell{.x = 25, .y = 5},
                             .target_heading = kNorth + (kFullTurn / 8.0F),
                             .bounty = 4973,
                             .stations_robbed = 7};
  screen_.Clear(0);
  DrawRoofText(player_font, target_font, text, screen_);
  // Each piece in its box of the text row, glyph by glyph: 8 pixels a character from row 4 on.
  const auto expect = [this](const BitmapFont& font, std::string_view shown, std::int16_t column) {
    for (auto index = std::size_t{0}; index < shown.size(); ++index) {
      const auto glyph = font.GetGlyph(shown[index]);
      for (auto row = std::uint16_t{0}; row < BitmapFont::kGlyphSize; ++row) {
        for (auto pixel = std::size_t{0}; pixel < BitmapFont::kGlyphSize; ++pixel) {
          ASSERT_EQ(GetPixel(static_cast<std::int16_t>(column + (index * BitmapFont::kGlyphSize) + pixel),
                             static_cast<std::int16_t>(4 + row)),
                    static_cast<std::uint8_t>(glyph.Row(row)[pixel] + kRoofOffset))
              << shown << " character " << index;
        }
      }
    }
  };
  expect(player_font, "11", 40);
  expect(player_font, "15", 64);
  expect(player_font, "N ", 88);
  expect(player_font, "04973", 120);
  expect(target_font, "07", 184);
  expect(target_font, "NW", 216);
  expect(target_font, "25", 240);
  expect(target_font, "05", 264);
}

TEST_F(DriverViewTest, TheLeftHandGoesForTheGunAndTheSightComesUp) {
  const auto& assets = manager_.Engine();
  auto mission = Mission{MakeRoad()};
  mission.Start(kMissions[4], AiPolicies{}, 7);
  auto view = DriverView{assets, screen_, mission.GetRoad()};
  view.Draw(mission);
  const auto wheel = GetScreenPixels();
  // The gun out: after the hand's three frames, the dashboard has changed and the sight stands
  // under the horizon in the middle of the view, 10 rows down.
  mission.Step(PlayerCommands{.toggle_aim = true});
  for (auto tick = 0; tick < 12; ++tick) {
    mission.Step(PlayerCommands{});
    view.Draw(mission);
  }
  const auto aiming = GetScreenPixels();
  auto dashboard_changed = false;
  auto view_changed = false;
  for (auto index = std::size_t{0}; index < aiming.size(); ++index) {
    const auto row = index / Screen::kWidth;
    const auto column = index % Screen::kWidth;
    if (aiming[index] != wheel[index]) {
      dashboard_changed = dashboard_changed or row >= kDashboardRow;
      if (row < kDashboardRow) {
        view_changed = true;
        // Nothing moves in the view but the sight.
        ASSERT_GT(row, 60U);
        ASSERT_LT(row, 100U);
        ASSERT_GT(column, 130U);
        ASSERT_LT(column, 190U);
      }
    }
  }
  EXPECT_TRUE(dashboard_changed);
  EXPECT_TRUE(view_changed);
  // Put away, the hand is back on the wheel and the sight is gone. The roof's text has moved on
  // meanwhile: the bounty drains.
  mission.Step(PlayerCommands{.toggle_aim = true});
  for (auto tick = 0; tick < 12; ++tick) {
    mission.Step(PlayerCommands{});
    view.Draw(mission);
  }
  const auto away = GetScreenPixels();
  for (auto index = std::size_t{60} * Screen::kWidth; index < away.size(); ++index) {
    ASSERT_EQ(away[index], wheel[index]) << "row " << index / Screen::kWidth << " column " << index % Screen::kWidth;
  }
}

TEST_F(DriverViewTest, TheHandsFollowTheWheel) {
  const auto& cockpit = manager_.Engine().Bank(EngineBank::kCockpit);
  const auto draw = [this, &cockpit](float steer, bool shaken) {
    screen_.EnableSplit(kDashboardRow);
    screen_.SetViewport(Viewport::kLower);
    DrawDashboard(cockpit, DashboardInput{.steer = steer, .shaken = shaken}, screen_);
    auto pixels = std::array<std::uint8_t, std::size_t{Screen::kHeight - kDashboardRow} * Screen::kWidth>{};
    for (auto row = kDashboardRow; row < Screen::kHeight; ++row) {
      std::ranges::copy(screen_.ScreenRow(row),
                        pixels.begin() + static_cast<std::ptrdiff_t>((row - kDashboardRow) * Screen::kWidth));
    }
    return pixels;
  };
  const auto straight = draw(0.0F, false);
  EXPECT_EQ(draw(0.0F, false), straight);
  EXPECT_NE(draw(1.0F, false), straight);
  EXPECT_NE(draw(-1.0F, false), straight);
  EXPECT_NE(draw(0.0F, true), straight);
  // At rest both hands hold the wheel at the same height: their skin (color 15) starts on the
  // same row, left and right of the hub.
  auto left = 0;
  auto right = 0;
  for (auto column = std::size_t{0}; column < Screen::kWidth; ++column) {
    const auto skin = straight[(std::size_t{50} * Screen::kWidth) + column] == 15;
    left += skin and column < 160 ? 1 : 0;
    right += skin and column >= 160 ? 1 : 0;
  }
  EXPECT_GT(left, 0);
  EXPECT_GT(right, 0);
}

TEST_F(DriverViewTest, TheBackdropStandsOnTheHorizonAndTurnsWithTheHeading) {
  const auto& backdrop = manager_.Engine().Bank(EngineBank::kBackdrop);
  DrawSkyHorizon(kDriverView, backdrop, kNorth, 0.0F, screen_);
  const auto ahead = GetViewPixels();
  auto backdrop_pixels = 0;
  for (auto row = std::int16_t{0}; row < kDriverView.horizon_row; ++row) {
    for (auto column = std::int16_t{0}; column < Screen::kWidth; ++column) {
      const auto pixel = GetPixel(column, row);
      if (row < kBackdropRow) {
        ASSERT_EQ(pixel, GetSkyEntry(row)) << "row " << row << " column " << column;
      } else if (pixel != GetSkyEntry(row)) {
        // The backdrop's own colors, its sky left to the row's shade.
        ASSERT_LT(pixel, kColorRegisterCount) << "row " << row << " column " << column;
        ASSERT_NE(pixel, kSkyColor) << "row " << row << " column " << column;
        ++backdrop_pixels;
      }
    }
  }
  EXPECT_GT(backdrop_pixels, 0);

  // A full turn brings the same view back; a quarter turn does not.
  DrawSkyHorizon(kDriverView, backdrop, kNorth + kFullTurn, 0.0F, screen_);
  EXPECT_EQ(GetViewPixels(), ahead);
  DrawSkyHorizon(kDriverView, backdrop, 0.0F, 0.0F, screen_);
  EXPECT_NE(GetViewPixels(), ahead);

  // With the eye 60 units up the backdrop sinks 6 rows.
  DrawSkyHorizon(kDriverView, backdrop, kNorth, 60.0F, screen_);
  for (auto row = kBackdropRow; row < kBackdropRow + 6; ++row) {
    for (auto column = std::int16_t{0}; column < Screen::kWidth; ++column) {
      ASSERT_EQ(GetPixel(column, static_cast<std::int16_t>(row)), GetSkyEntry(static_cast<std::int16_t>(row)));
    }
  }
}

TEST_F(DriverViewTest, DrawsWhatStandsBesideTheRoad) {
  const auto& assets = manager_.Engine();
  const auto road = MakeRoad();
  auto objects = RoadsideObjects{};
  // 800 units south of the first stone of the straight at (13, 2), looking at it.
  const auto cell = Cell{.x = 13, .y = 2};
  const auto scenery = road.GetScenery(cell);
  const auto stone =
      std::ranges::find_if(scenery, [](const PlacedObject& object) { return object.type >= 3 and object.type <= 6; });
  ASSERT_NE(stone, scenery.end());
  const auto input =
      ViewInput{.position = GetCellOrigin(cell) +
                            WorldPoint{.x = static_cast<float>(stone->x), .y = static_cast<float>(stone->y)} -
                            WorldPoint{.x = 0.0F, .y = 800.0F},
                .heading = kNorth,
                .eye_height = DriverView::kEyeHeight};
  screen_.Clear(0);
  objects.Draw(kDriverView, input, road, {}, assets, screen_);
  // The stone stands straight ahead, 100 * 256 / 800 = 32 rows under the horizon.
  auto drawn = 0;
  for (auto row = std::int16_t{92}; row <= 104; ++row) {
    for (auto column = std::int16_t{150}; column <= 170; ++column) {
      drawn += GetPixel(column, row) != 0 ? 1 : 0;
    }
  }
  EXPECT_GT(drawn, 0);
  // In the colors of the ground's haze at its depth.
  const auto pixels = GetViewPixels();
  EXPECT_GE(*std::ranges::max_element(pixels), GetGroundOffset(102));
}

TEST_F(DriverViewTest, AStationSignShowsItsBoardFromTheRoad) {
  const auto& assets = manager_.Engine();
  const auto road = MakeRoad();
  auto objects = RoadsideObjects{};
  // The station at (32, 5), driving north past its first sign, which stands 3600 ahead on the right.
  const auto input = ViewInput{.position = GetCellOrigin(Cell{.x = 32, .y = 5}) + WorldPoint{.x = 8448, .y = 1000},
                               .heading = kNorth,
                               .eye_height = DriverView::kEyeHeight};
  screen_.Clear(0);
  objects.Draw(kDriverView, input, road, {}, assets, screen_);
  // The board and the arrow are white and red (colors 1 and 11) well above the horizon.
  auto red = 0;
  for (auto row = std::int16_t{40}; row < 66; ++row) {
    for (auto column = std::int16_t{200}; column < 230; ++column) {
      red += (GetPixel(column, row) % kColorRegisterCount) == 11 ? 1 : 0;
    }
  }
  EXPECT_GT(red, 0);
}

TEST_F(DriverViewTest, DrawsACarAheadInItsOwnColors) {
  const auto& assets = manager_.Engine();
  // The real road without its scenery, so the car is all there is to draw.
  const auto road = Road{assets.road_map, assets.road_shapes, Scenery{}};
  auto objects = RoadsideObjects{};
  const auto eye = WorldPoint{.x = 10.5F * kCellUnits, .y = 10.5F * kCellUnits};
  const auto input = ViewInput{.position = eye, .heading = kNorth, .eye_height = DriverView::kEyeHeight};
  // 1000 units straight ahead and driving away: its back at the third size, the picture's hotspot
  // 100 * 256 / 1000 = 25 rows under the horizon in the middle column.
  const auto& back = assets.Bank(EngineBank::kCar6).sprites[2];
  const auto left = 160 - back.origin_x;
  const auto top = 95 - back.origin_y;
  const auto cars = std::to_array<RoadsideObjects::Car>(
      {{.position = eye + WorldPoint{.y = 1000.0F}, .heading = kNorth, .index_offset = kTrafficFirstOffset}});
  screen_.Clear(0);
  objects.Draw(kDriverView, input, road, cars, assets, screen_);
  auto body = 0;
  auto paint = 0;
  for (auto row = std::int16_t{0}; row < static_cast<std::int16_t>(kViewRows); ++row) {
    for (auto column = std::int16_t{0}; column < Screen::kWidth; ++column) {
      if (const auto pixel = GetPixel(column, row); pixel != 0) {
        // Nothing but the car's colors, and only where the car stands.
        ASSERT_GT(pixel, kTrafficFirstOffset);
        ASSERT_LT(pixel, kTrafficFirstOffset + kCarColorCount);
        ASSERT_GE(row, top);
        ASSERT_LT(row, top + back.height);
        ASSERT_GE(column, left);
        ASSERT_LT(column, left + back.width);
        ++body;
        paint += pixel >= kTrafficFirstOffset + kCarPaintFirst ? 1 : 0;
      }
    }
  }
  EXPECT_GT(body, 200);
  EXPECT_GT(paint, 50);
  // Behind the eye, or past the depth drawn, it is left out.
  screen_.Clear(0);
  const auto behind = std::to_array<RoadsideObjects::Car>(
      {{.position = eye - WorldPoint{.y = 1000.0F}}, {.position = eye + WorldPoint{.y = 2.0F * kCellUnits}}});
  objects.Draw(kDriverView, input, road, behind, assets, screen_);
  const auto pixels = GetViewPixels();
  EXPECT_EQ(*std::ranges::max_element(pixels), 0);
}

TEST_F(DriverViewTest, ACarsFlanksAreEachOthersMirrorImage) {
  const auto& assets = manager_.Engine();
  const auto road = Road{assets.road_map, assets.road_shapes, Scenery{}};
  auto objects = RoadsideObjects{};
  const auto eye = WorldPoint{.x = 10.5F * kCellUnits, .y = 10.5F * kCellUnits};
  const auto input = ViewInput{.position = eye, .heading = kNorth, .eye_height = DriverView::kEyeHeight};
  // The same car straight ahead, crossing to the right, then to the left.
  const auto draw = [&](float heading) {
    const auto cars =
        std::to_array<RoadsideObjects::Car>({{.position = eye + WorldPoint{.y = 1000.0F}, .heading = heading}});
    screen_.Clear(0);
    objects.Draw(kDriverView, input, road, cars, assets, screen_);
    return GetViewPixels();
  };
  const auto rightward = draw(0.0F);
  const auto leftward = draw(kFullTurn / 2.0F);
  EXPECT_GT(std::ranges::count_if(rightward, [](std::uint8_t pixel) { return pixel != 0; }), 200);
  // Flipped about the car's place, between columns 159 and 160.
  for (auto row = std::size_t{0}; row < kViewRows; ++row) {
    for (auto column = std::size_t{0}; column < Screen::kWidth; ++column) {
      ASSERT_EQ(rightward[(row * Screen::kWidth) + column],
                leftward[(row * Screen::kWidth) + (Screen::kWidth - 1 - column)])
          << "row " << row << " column " << column;
    }
  }
  // And not its own mirror image: a flank has a front and a back.
  EXPECT_NE(rightward, leftward);
}

TEST_F(DriverViewTest, TheCarsHaveTheirPaintInThePalette) {
  const auto& assets = manager_.Engine();
  auto mission = Mission{MakeRoad()};
  mission.Start(kMissions[0], AiPolicies{}, 7);
  // One tick brings the traffic car.
  mission.Step(PlayerCommands{});
  ASSERT_EQ(mission.GetTraffic().size(), 1U);
  auto view = DriverView{assets, screen_, mission.GetRoad()};
  view.Draw(mission);
  const auto& palette = screen_.Palette(Viewport::kUpper);
  // The criminal's red in the view's set; the traffic car's scheme in its copy, after the white,
  // black and gray both share.
  EXPECT_EQ(palette.Color(kCarPaintFirst), kCarPaints[0][0]);
  const auto scheme = std::size_t{mission.GetTraffic().front().color_scheme};
  EXPECT_EQ(palette.Color(static_cast<std::uint8_t>(kTrafficFirstOffset + kCarPaintFirst)), kCarPaints[scheme][0]);
  EXPECT_EQ(palette.Color(static_cast<std::uint8_t>(kTrafficFirstOffset + 1)), palette.Color(1));
  EXPECT_EQ(palette.Color(static_cast<std::uint8_t>(kTrafficFirstOffset + 3)), palette.Color(3));
}

}  // namespace
}  // namespace hp2
