#include "core/sky_horizon.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/image_view.hpp"
#include "core/view_palette.hpp"
#include "core/world.hpp"

namespace hp2 {

namespace {

// Draws `strip` with its left edge at `column` and its top at `top`, on the rows above the horizon
// and the columns of the window.
void DrawStrip(const ViewDescription& view, const ImageView& strip, std::int32_t column, std::int32_t top,
               Screen& screen) {
  const auto first_column = std::max<std::int32_t>(column, view.left_column);
  const auto end_column = std::min<std::int32_t>(column + strip.width, view.right_column + 1);
  const auto first_row = std::max<std::int32_t>(top, view.top_row);
  const auto end_row = std::min<std::int32_t>(top + strip.height, view.horizon_row);
  for (auto row = first_row; row < end_row; ++row) {
    const auto screen_row = static_cast<std::int16_t>(row);
    const auto sky = GetSkyEntry(screen_row);
    const auto source = strip.Row(static_cast<std::uint16_t>(row - top));
    const auto pixels = screen.Row(static_cast<std::uint16_t>(row));
    for (auto at = first_column; at < end_column; ++at) {
      const auto index = source[static_cast<std::size_t>(at - column)];
      pixels[static_cast<std::size_t>(at)] = index == kSkyColor ? sky : index;
    }
  }
}

}  // namespace

void DrawSkyHorizon(const ViewDescription& view, const SpriteBank& backdrop, float heading, float lift,
                    Screen& screen) {
  for (auto row = view.top_row; row < view.horizon_row; ++row) {
    screen.DrawHorizontalLine(view.left_column, view.right_column, row, GetSkyEntry(row));
  }
  if (backdrop.sprites.size() >= kBackdropStripCount) {
    // The strips stand side by side around the eye. Turning left, the heading growing, slides them
    // to the right: strip 0 starts where the turn has brought it, less a full turn, and the ones
    // that have left the window on the left come back on the right.
    const auto strip_width = std::int32_t{backdrop.sprites[0].width};
    const auto turn_width = strip_width * static_cast<std::int32_t>(kBackdropStripCount);
    const auto turn = heading / kFullTurn;
    const auto pan = static_cast<std::int32_t>((turn - std::floor(turn)) * static_cast<float>(turn_width));
    const auto top = kBackdropRow + static_cast<std::int32_t>(std::max(lift, 0.0F) / kBackdropLiftUnits);
    for (auto strip = std::size_t{0}; strip < kBackdropStripCount; ++strip) {
      const auto image = backdrop.GetImage(strip);
      auto column = (static_cast<std::int32_t>(strip) * strip_width) + pan - turn_width;
      if (column + strip_width <= view.left_column) {
        column += turn_width;
      }
      if (image.width == strip_width) {
        DrawStrip(view, image, column, top, screen);
      }
    }
  }
}

}  // namespace hp2
