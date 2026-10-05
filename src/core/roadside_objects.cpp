#include "core/roadside_objects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numbers>
#include <span>

#include "core/sprite_bank.hpp"
#include "core/view_palette.hpp"

namespace hp2 {

namespace {

// The object types of COOR_OBJ.BIN (docs/formats.md).
constexpr auto kCactus = std::uint16_t{1};
constexpr auto kFirstStone = std::uint16_t{3};
constexpr auto kLastStone = std::uint16_t{6};
constexpr auto kBush = std::uint16_t{7};
constexpr auto kSignLeft = std::uint16_t{8};
constexpr auto kSignJunction = std::uint16_t{9};
constexpr auto kSignRight = std::uint16_t{10};
constexpr auto kStationSign = std::uint16_t{11};

// Every bank holds its pictures at 10 sizes, the largest first. The depth ahead picks the size in
// steps: 64 units a step from 256 on, 128 from 512, 256 from 768 (DrawObjects, 0:26e8). The size
// of each step is the same for everything but the cars (0:8148, 0:830c, 0:8356); past the last
// step the smallest size stays.
constexpr auto kSizes = std::to_array<std::uint8_t>(
    {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9});
// The stones' bank holds four kinds at four sizes each, and a stone is drawn only on the steps the
// four sizes cover, up to 1792 units (0:81dc and the three tables after it).
constexpr auto kStoneSizeCount = std::size_t{4};
constexpr auto kStoneStepCount = std::size_t{10};
constexpr auto kStoneDepth = 1792.0F;

// What stands further aside than this from the window's edge is left out, pixels: the widest
// picture is 64 wide.
constexpr auto kSideMargin = 64.0F;

// The two poles of a station's sign stand this far either side of it, by size (0:2d92).
struct PolePair {
  std::int8_t first;
  std::int8_t second;
};
constexpr auto kStationPoles = std::to_array<PolePair>({
    {.first = 15, .second = -15},
    {.first = 13, .second = -13},
    {.first = 8, .second = -8},
    {.first = 5, .second = -5},
    {.first = 4, .second = -4},
    {.first = 3, .second = -3},
    {.first = 2, .second = -2},
    {.first = 2, .second = -1},
    {.first = 1, .second = -1},
    {.first = 1, .second = 0},
});

// The side of a sign the eye sees.
enum class SignView : std::uint8_t { kEdge, kBack, kFront };

// The step of the size table a depth falls in.
std::size_t GetDepthStep(float depth) {
  constexpr auto kFirstDepth = 256.0F;
  constexpr auto kStepUnits = 64.0F;
  const auto steps = static_cast<std::size_t>(std::max(depth - kFirstDepth, 0.0F) / kStepUnits);
  auto step = steps;
  if (steps >= 4) {
    step = steps / 2 < 4 ? (steps / 2) + 2 : (steps / 4) + 4;
  }
  return std::min(step, kSizes.size() - 1);
}

// A sign faces along its angle: looking the opposite way the eye sees its front, looking along it
// its back, and from either side its edge, a quarter turn each (0:2b50).
SignView GetSignView(float heading, std::uint16_t sign_angle) {
  constexpr auto kQuarterTurn = std::numbers::pi_v<float> / 2.0F;
  const auto turned = std::abs(WrapAngle(heading - FromHalfDegrees(sign_angle)));
  auto view = SignView::kEdge;
  if (turned < kQuarterTurn / 2.0F) {
    view = SignView::kBack;
  } else if (turned >= kQuarterTurn * 1.5F) {
    view = SignView::kFront;
  }
  return view;
}

// Draws image `image` of `bank` with its hotspot at `foot`; nothing when the bank lacks it.
void DrawImage(const SpriteBank& bank, std::size_t image, Point foot, std::uint8_t index_offset, Screen& screen) {
  if (image < bank.sprites.size()) {
    const auto& sprite = bank.sprites[image];
    screen.BlitMasked(bank.GetImage(image),
                      Point{.x = static_cast<std::int16_t>(foot.x - sprite.origin_x),
                            .y = static_cast<std::int16_t>(foot.y - sprite.origin_y)},
                      index_offset);
  }
}

// A road sign on its pole: the face the eye sees, then the pole under it.
void DrawRoadSign(EngineBank front, SignView sign_view, std::size_t size, Point foot, std::uint8_t index_offset,
                  const EngineAssets& assets, Screen& screen) {
  auto bank = EngineBank::kSignEdge;
  if (sign_view == SignView::kBack) {
    bank = EngineBank::kSignBack;
  } else if (sign_view == SignView::kFront) {
    bank = front;
  }
  DrawImage(assets.Bank(bank), size, foot, index_offset, screen);
  DrawImage(assets.Bank(EngineBank::kSignPole), size, foot, index_offset, screen);
}

// A station's sign: from the side its edge on one pole; from the front or the back the arrow
// toward the station under the board, on two poles.
void DrawStationSign(SignView sign_view, std::size_t size, Point foot, std::uint8_t index_offset,
                     const EngineAssets& assets, Screen& screen) {
  const auto& pole = assets.Bank(EngineBank::kSignPole);
  if (sign_view == SignView::kEdge) {
    DrawImage(assets.Bank(EngineBank::kStationEdge), size, foot, index_offset, screen);
    DrawImage(pole, size, foot, index_offset, screen);
  } else {
    const auto arrow = sign_view == SignView::kBack ? EngineBank::kStationArrowLeft : EngineBank::kStationArrowRight;
    DrawImage(assets.Bank(arrow), size, foot, index_offset, screen);
    DrawImage(assets.Bank(EngineBank::kStationBoard), size, foot, index_offset, screen);
    const auto poles = kStationPoles[std::min(size, kStationPoles.size() - 1)];
    DrawImage(pole, size, Point{.x = static_cast<std::int16_t>(foot.x + poles.first), .y = foot.y}, index_offset,
              screen);
    DrawImage(pole, size, Point{.x = static_cast<std::int16_t>(foot.x + poles.second), .y = foot.y}, index_offset,
              screen);
  }
}

}  // namespace

void RoadsideObjects::Collect(const ViewDescription& view, const ViewInput& input, const Road& road) {
  count_ = 0;
  const auto axes = GetViewAxes(input.heading);
  const auto cells = GetViewCells(view, input, kDepth);
  const auto leftmost = static_cast<float>(view.left_column) - kSideMargin;
  const auto rightmost = static_cast<float>(view.right_column) + kSideMargin;
  for (auto cell_y = cells.first.y; cell_y <= cells.last.y; ++cell_y) {
    for (auto cell_x = cells.first.x; cell_x <= cells.last.x; ++cell_x) {
      const auto cell = Cell{.x = cell_x, .y = cell_y};
      const auto origin = GetCellOrigin(cell);
      for (const auto& placed : road.GetScenery(cell)) {
        const auto point =
            origin + WorldPoint{.x = static_cast<float>(placed.x), .y = static_cast<float>(placed.y)} - input.position;
        const auto depth = Dot(point, axes.forward);
        const auto stone = placed.type >= kFirstStone and placed.type <= kLastStone;
        if (depth >= kNearestDepth and depth < (stone ? kStoneDepth : kDepth) and count_ < kMaxObjects) {
          const auto offset = Dot(point, axes.right);
          const auto column = view.center_column + (offset * view.focal_length / depth);
          if (column >= leftmost and column <= rightmost) {
            objects_[count_] = ViewObject{.depth = depth, .offset = offset, .type = placed.type, .extra = placed.extra};
            ++count_;
          }
        }
      }
    }
  }
  std::ranges::sort(std::span{objects_}.first(count_), std::ranges::greater{}, &ViewObject::depth);
}

void RoadsideObjects::Draw(const ViewDescription& view, const ViewInput& input, const Road& road,
                           const EngineAssets& assets, Screen& screen) {
  Collect(view, input, road);
  for (const auto& object : std::span{objects_}.first(count_)) {
    // Where the object stands on the ground, its size, and the haze of the ground there.
    const auto scale = view.focal_length / object.depth;
    const auto foot = Point{
        .x = static_cast<std::int16_t>(std::floor(view.center_column + (object.offset * scale))),
        .y = static_cast<std::int16_t>(std::floor(static_cast<float>(view.horizon_row) + (input.eye_height * scale)))};
    const auto step = GetDepthStep(object.depth);
    const auto size = std::size_t{kSizes[step]};
    const auto index_offset = GetGroundOffset(std::min(foot.y, view.bottom_row));
    if (object.type >= kFirstStone and object.type <= kLastStone) {
      if (step < kStoneStepCount) {
        const auto kind = static_cast<std::size_t>(object.type - kFirstStone);
        DrawImage(assets.Bank(EngineBank::kStones), (kind * kStoneSizeCount) + size, foot, index_offset, screen);
      }
    } else if (object.type == kCactus) {
      DrawImage(assets.Bank(EngineBank::kCactus), size, foot, index_offset, screen);
    } else if (object.type == kBush) {
      DrawImage(assets.Bank(EngineBank::kBush), size, foot, index_offset, screen);
    } else if (object.type == kSignLeft) {
      DrawRoadSign(EngineBank::kSignLeft, GetSignView(input.heading, object.extra), size, foot, index_offset, assets,
                   screen);
    } else if (object.type == kSignJunction) {
      DrawRoadSign(EngineBank::kSignJunction, GetSignView(input.heading, object.extra), size, foot, index_offset,
                   assets, screen);
    } else if (object.type == kSignRight) {
      DrawRoadSign(EngineBank::kSignRight, GetSignView(input.heading, object.extra), size, foot, index_offset, assets,
                   screen);
    } else if (object.type == kStationSign) {
      DrawStationSign(GetSignView(input.heading, object.extra), size, foot, index_offset, assets, screen);
    }
  }
}

}  // namespace hp2
