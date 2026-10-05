#include "core/road_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/view_palette.hpp"

namespace hp2 {

namespace {

// Whether a dash of the edge lines lies at `depth` once the car has driven `travel`.
bool HasDash(float depth, float travel) {
  const auto along = std::fmod(depth + travel, kDashPeriodUnits);
  return (along < 0.0F ? along + kDashPeriodUnits : along) < kDashUnits;
}

}  // namespace

void DrawRoadSurface(const RoadRows& rows, float travel, Screen& screen) {
  for (auto screen_row = rows.horizon_row; screen_row <= rows.bottom_row; ++screen_row) {
    const auto& row = rows.row[rows.Index(screen_row)];
    const auto offset = GetGroundOffset(screen_row);
    const auto sand = static_cast<std::uint8_t>(offset + kSandColor);
    const auto asphalt = static_cast<std::uint8_t>(offset + kAsphaltColor);
    const auto line = static_cast<std::uint8_t>(offset + kLineColor);
    screen.DrawHorizontalLine(rows.left_column, rows.right_column, screen_row, sand);
    const auto lined = row.depth <= kLineDepth and HasDash(row.depth, travel);
    const auto line_width = std::max(1.0F, std::round(kLineUnits * row.scale));
    for (const auto& span : row.GetSpans()) {
      const auto first = rows.Column(span.left);
      const auto last = static_cast<std::int16_t>(rows.Column(span.right) - 1);
      screen.DrawHorizontalLine(first, last, screen_row, asphalt);
      if (lined) {
        // Each line stays within its span; an end outside the window keeps its line out there.
        const auto start_line_last = rows.Column(std::min(span.left + line_width, span.right)) - 1;
        const auto end_line_first = rows.Column(std::max(span.right - line_width, span.left));
        screen.DrawHorizontalLine(first, static_cast<std::int16_t>(start_line_last), screen_row, line);
        screen.DrawHorizontalLine(end_line_first, last, screen_row, line);
      }
    }
  }
}

}  // namespace hp2
