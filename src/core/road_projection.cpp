#include "core/road_projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace hp2 {

namespace {

// The lines across the view are at most this far apart in depth, units: half the road's width, so
// a road across the view is met by a line wherever it lies.
constexpr auto kLineSpacing = 512.0F;
// Spans nearer than this to each other join, pixels: the stretches of one road found in two cells
// or on two lines of a row.
constexpr auto kJoinGap = 0.5F;
// The longest outline has 53 points (the curves); a longer one is cut short.
constexpr auto kMaxOutlinePoints = std::size_t{64};
// The crossings of one line with one outline; the ones past these are left out.
constexpr auto kMaxCrossings = std::size_t{16};

// The lines of one row: `count` depths, from `first` on, `step` apart.
struct RowLines {
  float first{};
  float step{};
  std::uint16_t count{};
};

// A road outline as the eye sees it: how far ahead each point lies and how far to the right, and
// the depths the outline lies between.
struct ViewOutline {
  std::array<float, kMaxOutlinePoints> depths{};
  std::array<float, kMaxOutlinePoints> offsets{};
  std::size_t count{};
  float nearest{std::numeric_limits<float>::max()};
  float farthest{std::numeric_limits<float>::lowest()};
};

// Adds `span` to the row, joined with the spans it overlaps or nearly touches; left out when the
// row is full.
void AddSpan(RoadRow& row, RoadSpan span) {
  auto kept = std::size_t{0};
  for (auto index = std::size_t{0}; index < row.span_count; ++index) {
    const auto other = row.spans[index];
    if (other.right + kJoinGap < span.left or span.right + kJoinGap < other.left) {
      row.spans[kept] = other;
      ++kept;
    } else {
      span = RoadSpan{.left = std::min(span.left, other.left), .right = std::max(span.right, other.right)};
    }
  }
  if (kept < RoadRow::kMaxSpans) {
    // The spans kept are still in order: the new one goes to its place among them.
    auto place = kept;
    while (place > 0 and row.spans[place - 1].left > span.left) {
      row.spans[place] = row.spans[place - 1];
      --place;
    }
    row.spans[place] = span;
    ++kept;
  }
  row.span_count = static_cast<std::uint8_t>(kept);
}

// The outline of the cell whose south-west corner is at `origin`, seen from `eye`.
ViewOutline ToViewOutline(std::span<const ShapePoint> outline, WorldPoint origin, WorldPoint eye,
                          const ViewAxes& axes) {
  auto result = ViewOutline{};
  result.count = std::min(outline.size(), kMaxOutlinePoints);
  for (auto index = std::size_t{0}; index < result.count; ++index) {
    // Whole numbers of units until the eye is subtracted: a point two cells share comes out the
    // same from both.
    const auto point =
        origin + WorldPoint{.x = static_cast<float>(outline[index].x), .y = static_cast<float>(outline[index].y)};
    const auto depth = Dot(point - eye, axes.forward);
    result.depths[index] = depth;
    result.offsets[index] = Dot(point - eye, axes.right);
    result.nearest = std::min(result.nearest, depth);
    result.farthest = std::max(result.farthest, depth);
  }
  return result;
}

// Adds to `row` the stretches of road the line across the view at `depth` meets inside `outline`.
void AddLine(const ViewDescription& view, const ViewOutline& outline, float depth, RoadRow& row) {
  auto crossings = std::array<float, kMaxCrossings>{};
  auto found = std::size_t{0};
  for (auto index = std::size_t{1}; index < outline.count; ++index) {
    const auto start = outline.depths[index - 1];
    const auto end = outline.depths[index];
    if ((start > depth) != (end > depth) and found < kMaxCrossings) {
      const auto along = (depth - start) / (end - start);
      crossings[found] = outline.offsets[index - 1] + (along * (outline.offsets[index] - outline.offsets[index - 1]));
      ++found;
    }
  }
  std::ranges::sort(std::span{crossings}.first(found));
  // Even-odd: the road lies between the first crossing and the second, the third and the fourth.
  const auto scale = view.focal_length / depth;
  for (auto index = std::size_t{1}; index < found; index += 2) {
    const auto span = RoadSpan{.left = view.center_column + (crossings[index - 1] * scale),
                               .right = view.center_column + (crossings[index] * scale)};
    if (span.right > static_cast<float>(view.left_column) and span.left < static_cast<float>(view.right_column + 1)) {
      AddSpan(row, span);
    }
  }
}

// The lines of row `index`. A point on the ground `depth` ahead shows row_depth / depth below the
// horizon, row_depth being the eye's height times the focal length: the row holds the depths
// between the ones of its top and bottom edges, out to the view's depth. An eye at or under the
// ground has no lines.
RowLines GetRowLines(const ViewDescription& view, float row_depth, std::size_t index) {
  auto lines = RowLines{};
  const auto nearest = row_depth / static_cast<float>(index + 1);
  const auto farthest = index == 0 ? view.depth : std::min(view.depth, row_depth / static_cast<float>(index));
  if (nearest > 0.0F and farthest > nearest) {
    const auto count = std::ceil((farthest - nearest) / kLineSpacing);
    const auto step = (farthest - nearest) / count;
    lines = RowLines{.first = nearest + (step / 2.0F), .step = step, .count = static_cast<std::uint16_t>(count)};
  }
  return lines;
}

// Adds to the first `row_count` rows the road inside `outline`, on every line that crosses it.
void AddOutline(const ViewDescription& view, const ViewOutline& outline, float row_depth, std::size_t row_count,
                RoadRows& rows) {
  for (auto index = std::size_t{0}; index < row_count; ++index) {
    const auto lines = GetRowLines(view, row_depth, index);
    for (auto line = std::uint16_t{0}; line < lines.count; ++line) {
      const auto depth = lines.first + (static_cast<float>(line) * lines.step);
      if (depth > outline.nearest and depth < outline.farthest) {
        AddLine(view, outline, depth, rows.row[index]);
      }
    }
  }
}

}  // namespace

CellRange GetViewCells(const ViewDescription& view, const ViewInput& input, float depth) {
  const auto axes = GetViewAxes(input.heading);
  const auto reach = depth / view.focal_length;
  const auto far_middle = input.position + (axes.forward * depth);
  const auto far_left =
      far_middle - (axes.right * (reach * (view.center_column - static_cast<float>(view.left_column))));
  const auto far_right =
      far_middle + (axes.right * (reach * (static_cast<float>(view.right_column + 1) - view.center_column)));
  return CellRange{.first = GetCell(WorldPoint{.x = std::min({input.position.x, far_left.x, far_right.x}),
                                               .y = std::min({input.position.y, far_left.y, far_right.y})}),
                   .last = GetCell(WorldPoint{.x = std::max({input.position.x, far_left.x, far_right.x}),
                                              .y = std::max({input.position.y, far_left.y, far_right.y})})};
}

void RoadProjection::Project(const ViewInput& input, RoadRows& rows) const {
  rows.horizon_row = view_.horizon_row;
  rows.bottom_row = view_.bottom_row;
  rows.left_column = view_.left_column;
  rows.right_column = view_.right_column;
  const auto row_count =
      std::min(static_cast<std::size_t>(std::max(view_.bottom_row - view_.horizon_row + 1, 0)), kMaxViewRows);

  const auto row_depth = input.eye_height * view_.focal_length;
  for (auto index = std::size_t{0}; index < row_count; ++index) {
    auto& row = rows.row[index];
    row.span_count = 0;
    row.depth = row_depth / (static_cast<float>(index) + 0.5F);
    row.scale = row.depth > 0.0F ? view_.focal_length / row.depth : 0.0F;
  }

  const auto axes = GetViewAxes(input.heading);
  const auto cells = GetViewCells(view_, input, view_.depth);
  for (auto cell_y = cells.first.y; cell_y <= cells.last.y; ++cell_y) {
    for (auto cell_x = cells.first.x; cell_x <= cells.last.x; ++cell_x) {
      const auto cell = Cell{.x = cell_x, .y = cell_y};
      const auto outline = road_.GetOutline(cell);
      if (not outline.empty()) {
        AddOutline(view_, ToViewOutline(outline, GetCellOrigin(cell), input.position, axes), row_depth, row_count,
                   rows);
      }
    }
  }
}

}  // namespace hp2
