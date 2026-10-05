#pragma once

#include "core/screen.hpp"
#include "core/sprite_bank.hpp"

namespace hp2 {

// What the driver sees of the car, from the cockpit bank (DES_TABB.IMG): the dashboard with its
// needles, the steering wheel and the hands on it, the roof strip along the top of the screen, and
// the hood's edge above the dashboard (DrawDashboard 0:ce74, MaskBonnetEdge 0:d836). The gauges'
// warning lights and the hand that takes the gun are not drawn yet.

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
};

// The dashboard picture, the steering wheel over it, the needles of the four gauges, and the two
// hands, which follow the wheel. Draws into the selected viewport, the dashboard's, from its top
// row.
void DrawDashboard(const SpriteBank& cockpit, const DashboardInput& input, Screen& screen);

// The roof strip, on the first rows of the selected viewport, in the roof's colors.
void DrawRoofStrip(const SpriteBank& cockpit, Screen& screen);

// The hood's edge: black on the right of the last three rows above the dashboard, where the hood
// rises out of the dashboard picture. Draws into the selected viewport, the view's.
void DrawHoodEdge(Screen& screen);

}  // namespace hp2
