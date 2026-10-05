#pragma once

#include "core/bitmap_font.hpp"
#include "core/screen.hpp"
#include "core/sprite_bank.hpp"
#include "core/world.hpp"

namespace hp2 {

// What the driver sees of the car, from the cockpit bank (DES_TABB.IMG): the dashboard with its
// needles, the steering wheel and the hands on it, the roof strip along the top of the screen, and
// the hood's edge above the dashboard (DrawDashboard 0:ce74, MaskBonnetEdge 0:d836). The gauges'
// warning lights are not drawn yet.

// What the dashboard shows of the car.
struct DashboardInput {
  float steer{};        // the wheel's position, -1 full right to 1 full left
  float speed{};        // units per second; the needle shows its size
  float rpm{};          // as the original counts it, 0 to about 400 (docs/vehicles.md, section 4)
  float fuel{};         // 0 empty to 1 full
  float temperature{};  // 0 cold to 1 overheated
  // Lifts the wheel and the hands a pixel; alternating it while the car moves makes the original's
  // vibration.
  bool shaken{};
  // How far the left hand has left its place on the wheel for the gun: 0 on the wheel, 1 where it
  // holds the gun, which is where it would be at full left lock.
  float aim{};
};

// The dashboard picture, the steering wheel over it, the needles of the four gauges, and the two
// hands, which follow the wheel. Draws into the selected viewport, the dashboard's, from its top
// row.
void DrawDashboard(const SpriteBank& cockpit, const DashboardInput& input, Screen& screen);

// The roof strip, on the first rows of the selected viewport, in the roof's colors.
void DrawRoofStrip(const SpriteBank& cockpit, Screen& screen);

// What the roof strip's text shows.
struct RoofText {
  Cell player;
  float player_heading{};
  // The criminal's car.
  Cell target;
  float target_heading{};
  std::int32_t bounty{};
  int stations_robbed{};
};

// The roof strip's text (DrawHudText, 0:e1b4), left to right: the player's cell, column then row,
// and the way the car heads, one of eight compass points; the bounty, five digits; the stations
// robbed so far; the way the criminal's car heads and its cell. The player's and the bounty are in
// `player_font` (LETTRE2.BIN), the rest in `target_font` (LETTRE1.BIN), all in the roof's colors.
// Draws into the selected viewport, the view's.
void DrawRoofText(const BitmapFont& player_font, const BitmapFont& target_font, const RoofText& text, Screen& screen);

// The gun's sight (BALLE.IMG) with its hotspot at `position`, and over it the flash of a shot that
// hit when `hit`. Draws into the selected viewport, the view's.
void DrawSight(const SpriteBank& gun, Point position, bool hit, Screen& screen);

// A bullet hole in the windshield (BALLE.IMG) with its hotspot at `position`. Draws into the
// selected viewport, the view's.
void DrawBulletHole(const SpriteBank& gun, Point position, Screen& screen);

// The hood's edge: black on the right of the last three rows above the dashboard, where the hood
// rises out of the dashboard picture. Draws into the selected viewport, the view's.
void DrawHoodEdge(Screen& screen);

}  // namespace hp2
