#pragma once

#include "core/road_projection.hpp"
#include "core/screen.hpp"

namespace hp2 {

// The lines along the road's edges: kLineUnits wide, set just inside the road, in dashes of
// kDashUnits every kDashPeriodUnits, drawn up to kLineDepth ahead. The original reads the dashes
// from a table of 40 phases by 56 rows (roadStripeTable, 0:90e0) and the widths from another
// (roadStripeWidths, 1:2c54); these lengths are fitted to them.
inline constexpr auto kLineUnits = 26.0F;
inline constexpr auto kDashUnits = 420.0F;
inline constexpr auto kDashPeriodUnits = 1050.0F;
inline constexpr auto kLineDepth = 4300.0F;

// The ground of the driver's view, row by row from the horizon down: sand, the road's spans in
// asphalt, each in the colors of the row's haze, and over the road the white lines along its edges
// (the end of RenderRoadView, 0:dc36). The dashes lie at fixed depths that `travel`, the distance
// the car has driven, brings nearer. Draws into the selected viewport.
void DrawRoadSurface(const RoadRows& rows, float travel, Screen& screen);

}  // namespace hp2
