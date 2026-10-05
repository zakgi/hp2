#include "core/cockpit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string_view>

#include "core/view_palette.hpp"

namespace hp2 {

namespace {

// The images of the cockpit bank.
constexpr auto kWheelImage = std::size_t{0};
constexpr auto kLeftHandImage = std::size_t{1};
constexpr auto kRightHandImage = std::size_t{2};
constexpr auto kDashboardImage = std::size_t{3};
constexpr auto kRoofImage = std::size_t{4};
constexpr auto kImageCount = std::size_t{5};

// Where the wheel's picture goes on the dashboard: a row lower while not shaken.
constexpr auto kWheelColumn = std::int16_t{48};
constexpr auto kWheelRow = std::int16_t{1};

// The hands ride a circle about the wheel's hub, which lies below the screen: at rest 39 degrees
// above the horizontal either side, turning with the wheel 30 degrees either way at full lock, and
// sliding 9 more pixels down on the side the wheel turns to, up on the other. In the original the
// wheel's position (player +0c) is that angle in half degrees, 60 at full lock.
constexpr auto kHubColumn = 160.0F;
constexpr auto kHubRow = 103.0F;  // below the dashboard's top row
constexpr auto kHandRadius = 93.0F;
constexpr auto kDegree = std::numbers::pi_v<float> / 180.0F;
constexpr auto kHandRestAngle = 39.0F * kDegree;
constexpr auto kHandTurnAngle = 30.0F * kDegree;
constexpr auto kHandSlide = 9.0F;

// The two dials: a needle of kNeedleLength from the hub, in the dashboard's red with a darker line
// either side. Angles are counter-clockwise from the right, as the hands'. The speedometer's
// needle rests at 204 degrees and turns clockwise 99/84 half degrees for each unit a frame of the
// original's speed: 235.7 degrees at its 400, the port's 8000 units a second (docs/highway.md,
// "Time"). The tachometer's rests at 198 degrees and turns 98/88 half degrees for each of the
// original's units of engine speed.
constexpr auto kNeedleLength = 20.0F;
constexpr auto kNeedleColor = std::uint8_t{14};
constexpr auto kNeedleSideColor = std::uint8_t{13};
constexpr auto kSpeedometerHub = Point{.x = 131, .y = 47};
constexpr auto kSpeedometerRestAngle = 204.0F * kDegree;
constexpr auto kSpeedometerSweep = (400.0F * 99.0F / 84.0F / 2.0F) * kDegree;
constexpr auto kSpeedometerSpeed = 8000.0F;
constexpr auto kTachometerHub = Point{.x = 188, .y = 47};
constexpr auto kTachometerRestAngle = 198.0F * kDegree;
constexpr auto kTachometerTurn = (98.0F / 88.0F / 2.0F) * kDegree;
// A needle that points right, under 24 degrees, or left, from 115 on, has its darker lines above
// and below it, and one that points up has them left and right; pointing right, the whole needle
// starts a pixel further right.
constexpr auto kNeedleRightAngle = 24.0F * kDegree;
constexpr auto kNeedleLeftAngle = 115.0F * kDegree;

// The two small gauges: a line from the hub to one of 16 tips, the first for an empty tank or a
// cold engine (gaugeLeftNeedleTips 0:d7b6, gaugeRightNeedleTips 0:d7f6).
constexpr auto kGaugeTipCount = std::size_t{16};
constexpr auto kFuelHub = Point{.x = 93, .y = 65};
constexpr auto kFuelTips = std::to_array<Point>({
    {.x = 86, .y = 56},
    {.x = 86, .y = 55},
    {.x = 87, .y = 54},
    {.x = 88, .y = 53},
    {.x = 89, .y = 52},
    {.x = 90, .y = 51},
    {.x = 91, .y = 51},
    {.x = 92, .y = 51},
    {.x = 93, .y = 51},
    {.x = 94, .y = 51},
    {.x = 95, .y = 51},
    {.x = 96, .y = 51},
    {.x = 97, .y = 52},
    {.x = 98, .y = 53},
    {.x = 99, .y = 54},
    {.x = 100, .y = 55},
});
constexpr auto kTemperatureHub = Point{.x = 226, .y = 65};
constexpr auto kTemperatureTips = std::to_array<Point>({
    {.x = 219, .y = 55},
    {.x = 219, .y = 54},
    {.x = 220, .y = 53},
    {.x = 221, .y = 52},
    {.x = 222, .y = 52},
    {.x = 223, .y = 51},
    {.x = 224, .y = 51},
    {.x = 225, .y = 51},
    {.x = 226, .y = 51},
    {.x = 227, .y = 51},
    {.x = 228, .y = 51},
    {.x = 229, .y = 51},
    {.x = 230, .y = 52},
    {.x = 231, .y = 52},
    {.x = 232, .y = 53},
    {.x = 233, .y = 54},
});
static_assert(kFuelTips.size() == kGaugeTipCount);
static_assert(kTemperatureTips.size() == kGaugeTipCount);

// The roof strip's text stands on its fifth row; the player's cell goes in the first two boxes.
constexpr auto kRoofTextRow = std::int16_t{4};
constexpr auto kPlayerColumnText = std::int16_t{40};
constexpr auto kPlayerRowText = std::int16_t{64};

// The hood's edge: on each row, black from a column to the window's right edge.
struct HoodRun {
  std::int16_t row;
  std::int16_t first_column;
};
constexpr auto kHoodRuns = std::to_array<HoodRun>({
    {.row = 129, .first_column = 248},
    {.row = 130, .first_column = 172},
    {.row = 131, .first_column = 118},
});
constexpr auto kHoodColor = std::uint8_t{0};

// Draws the hand `image` with its hotspot on the circle at `angle`, `slide` pixels lower.
void DrawHand(const SpriteBank& cockpit, std::size_t image, float angle, float slide, std::int16_t lift,
              Screen& screen) {
  const auto& sprite = cockpit.sprites[image];
  const auto column = std::lround(kHubColumn + (kHandRadius * std::cos(angle)));
  const auto row = std::lround(kHubRow - (kHandRadius * std::sin(angle)) + slide);
  screen.BlitMasked(cockpit.GetImage(image), Point{.x = static_cast<std::int16_t>(column - sprite.origin_x),
                                                   .y = static_cast<std::int16_t>(row - sprite.origin_y - lift)});
}

// `point` moved by `columns` and `rows`.
Point Shift(Point point, int columns, int rows) {
  return Point{.x = static_cast<std::int16_t>(point.x + columns), .y = static_cast<std::int16_t>(point.y + rows)};
}

// Draws a dial's needle from `hub` along `angle`.
void DrawNeedle(Point hub, float angle, Screen& screen) {
  const auto pointing_right = angle < kNeedleRightAngle;
  const auto level = pointing_right or angle >= kNeedleLeftAngle;
  const auto start = Shift(hub, pointing_right ? 1 : 0, 0);
  const auto tip = Shift(start, static_cast<int>(std::lround(kNeedleLength * std::cos(angle))),
                         -static_cast<int>(std::lround(kNeedleLength * std::sin(angle))));
  const auto side_columns = level ? 0 : 1;
  const auto side_rows = level ? 1 : 0;
  screen.DrawLine(start, tip, kNeedleColor);
  screen.DrawLine(Shift(start, -side_columns, -side_rows), Shift(tip, -side_columns, -side_rows), kNeedleSideColor);
  screen.DrawLine(Shift(start, side_columns, side_rows), Shift(tip, side_columns, side_rows), kNeedleSideColor);
}

// Draws a small gauge's needle from `hub` to the tip `level`, 0 to 1, picks.
void DrawGauge(Point hub, std::span<const Point, kGaugeTipCount> tips, float level, Screen& screen) {
  const auto tip = static_cast<std::size_t>(std::clamp(level, 0.0F, 1.0F) * static_cast<float>(kGaugeTipCount));
  screen.DrawLine(hub, tips[std::min(tip, kGaugeTipCount - 1)], kNeedleColor);
}

// Draws `value`, held within 0 to 99, as two digits of the roof's text from `column` on.
void DrawTwoDigits(const BitmapFont& font, int value, std::int16_t column, Screen& screen) {
  constexpr auto kTen = 10;
  const auto shown = std::clamp(value, 0, (kTen * kTen) - 1);
  const auto digits =
      std::array<char, 2>{static_cast<char>('0' + (shown / kTen)), static_cast<char>('0' + (shown % kTen))};
  screen.DrawText(font, std::string_view{digits.data(), digits.size()}, Point{.x = column, .y = kRoofTextRow},
                  kRoofOffset);
}

}  // namespace

void DrawDashboard(const SpriteBank& cockpit, const DashboardInput& input, Screen& screen) {
  if (cockpit.sprites.size() >= kImageCount) {
    const auto lift = static_cast<std::int16_t>(input.shaken ? 1 : 0);
    screen.Blit(cockpit.GetImage(kDashboardImage), Point{});
    screen.BlitMasked(cockpit.GetImage(kWheelImage),
                      Point{.x = kWheelColumn, .y = static_cast<std::int16_t>(kWheelRow - lift)});
    DrawGauge(kFuelHub, kFuelTips, input.fuel, screen);
    DrawGauge(kTemperatureHub, kTemperatureTips, input.temperature, screen);
    DrawNeedle(kSpeedometerHub, kSpeedometerRestAngle - (kSpeedometerSweep * std::abs(input.speed) / kSpeedometerSpeed),
               screen);
    DrawNeedle(kTachometerHub, kTachometerRestAngle - (kTachometerTurn * input.rpm), screen);
    const auto turn = kHandTurnAngle * input.steer;
    DrawHand(cockpit, kLeftHandImage, std::numbers::pi_v<float> - kHandRestAngle + turn, kHandSlide * input.steer, lift,
             screen);
    DrawHand(cockpit, kRightHandImage, kHandRestAngle + turn, -kHandSlide * input.steer, lift, screen);
  }
}

void DrawRoofStrip(const SpriteBank& cockpit, Screen& screen) {
  if (cockpit.sprites.size() >= kImageCount) {
    screen.Blit(cockpit.GetImage(kRoofImage), Point{}, kRoofOffset);
  }
}

void DrawRoofText(const BitmapFont& font, Cell player, Screen& screen) {
  DrawTwoDigits(font, player.x, kPlayerColumnText, screen);
  DrawTwoDigits(font, player.y, kPlayerRowText, screen);
}

void DrawHoodEdge(Screen& screen) {
  for (const auto& run : kHoodRuns) {
    screen.DrawHorizontalLine(run.first_column, static_cast<std::int16_t>(Screen::kWidth - 1), run.row, kHoodColor);
  }
}

}  // namespace hp2
