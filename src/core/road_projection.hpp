#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/road.hpp"
#include "core/world.hpp"

namespace hp2 {

// The numbers of the driver's view: where the ground shows on the screen and how the world is
// projected onto it. A point `depth` units ahead of the eye, `offset` to its right and `drop` below
// it shows offset * focal_length / depth pixels right of center_column and drop * focal_length /
// depth pixels below the top of horizon_row. The window, the center and the focal length are the
// original's (ProjectPoint; docs/renderer.md, "Road pipeline"); the depth is the port's: the
// original draws the player's cell and one neighbor.
struct ViewDescription {
  std::int16_t left_column{};  // the window's columns, both included
  std::int16_t right_column{};
  std::int16_t top_row{};      // the first row of sky
  std::int16_t horizon_row{};  // the first row of ground
  std::int16_t bottom_row{};   // the last
  float center_column{};       // where a point straight ahead shows
  float focal_length{};        // pixels
  float depth{};               // how far ahead the ground is drawn, units
};

inline constexpr auto kMaxViewRows = std::size_t{62};

inline constexpr auto kDriverView = ViewDescription{.left_column = 0,
                                                    .right_column = 319,
                                                    .top_row = 0,
                                                    .horizon_row = 70,
                                                    .bottom_row = 131,
                                                    .center_column = 160.0F,
                                                    .focal_length = 256.0F,
                                                    .depth = 4.0F * kCellUnits};
static_assert(kDriverView.bottom_row - kDriverView.horizon_row + 1 <= static_cast<int>(kMaxViewRows));

// What the car contributes to the view: the driver's eye.
struct ViewInput {
  WorldPoint position;
  float heading{};     // where the driver looks, radians
  float eye_height{};  // above the ground, units
};

// The eye's axes on the map: unit vectors ahead and to the right.
struct ViewAxes {
  WorldPoint forward;
  WorldPoint right;
};

[[nodiscard]] inline ViewAxes GetViewAxes(float heading) {
  const auto forward = GetDirection(heading);
  return ViewAxes{.forward = forward, .right = WorldPoint{.x = forward.y, .y = -forward.x}};
}

// Cells from `first` to `last`, both included.
struct CellRange {
  Cell first;
  Cell last;
};

// The cells around what the eye of `input` sees out to `depth`: those the triangle from the eye to
// the two ends of the line across the view at that depth lies in.
[[nodiscard]] CellRange GetViewCells(const ViewDescription& view, const ViewInput& input, float depth);

// A stretch of road on a screen row, from `left` to `right` in pixel coordinates: not yet rounded,
// and not held within the window.
struct RoadSpan {
  float left{};
  float right{};
};

// One screen row of ground.
struct RoadRow {
  // A row that meets more stretches of road than this loses the ones found last.
  static constexpr auto kMaxSpans = std::size_t{8};

  float depth{};                            // of the row's middle ahead of the eye, units
  float scale{};                            // pixels to a unit at that depth
  std::array<RoadSpan, kMaxSpans> spans{};  // left to right, apart from each other
  std::uint8_t span_count{};

  [[nodiscard]] std::span<const RoadSpan> GetSpans() const { return std::span{spans}.first(span_count); }
};

// The ground of one view, row by row.
struct RoadRows {
  std::int16_t horizon_row{};  // the first row of ground; `row[0]` is it
  std::int16_t bottom_row{};   // the last
  std::int16_t left_column{};  // the window's columns, both included
  std::int16_t right_column{};
  std::array<RoadRow, kMaxViewRows> row{};

  // The row's index in `row`, for a screen row of the window.
  [[nodiscard]] std::size_t Index(std::int16_t screen_row) const {
    return static_cast<std::size_t>(screen_row - horizon_row);
  }
  // The first column whose middle lies at `coordinate` or right of it, held within the window's
  // columns plus one: a span fills the columns from Column(left) to just before Column(right).
  [[nodiscard]] std::int16_t Column(float coordinate) const {
    const auto low = static_cast<float>(left_column);
    const auto high = static_cast<float>(right_column + 1);
    return static_cast<std::int16_t>(std::clamp(std::ceil(coordinate - 0.5F), low, high));
  }
};

// The road projected into the driver's view. Each row of ground is a band of depth ahead of the
// eye. Lines across the view at depths inside the band, never further apart than half the road's
// width so that no road slips between two, are cut by the road outlines of the cells they cross,
// even-odd as Road::IsOnRoad; the stretches of road found on a row's lines, joined, are its spans.
//
// The original transforms, clips and projects the outline of the player's cell and of one neighbor
// and fills them by XOR (BuildRoadPolygons 0:d9ec, RenderRoadView 0:dc36). Here every cell out to
// the view's depth counts and nothing is clipped: a line's depth is always ahead of the eye.
class RoadProjection {
 public:
  RoadProjection(const ViewDescription& view, const Road& road) : view_(view), road_(road) {}

  // The view's rows as the eye of `input` sees them.
  void Project(const ViewInput& input, RoadRows& rows) const;

 private:
  const ViewDescription& view_;
  const Road& road_;
};

}  // namespace hp2
