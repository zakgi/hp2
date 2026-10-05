#pragma once

#include <cstddef>
#include <cstdint>

#include "core/road_projection.hpp"
#include "core/screen.hpp"
#include "core/sprite_bank.hpp"

namespace hp2 {

// The backdrop is six strips side by side, a full turn of the horizon (DEC_FOND.IMG).
inline constexpr auto kBackdropStripCount = std::size_t{6};
// The backdrop's top row while the eye is at rest; it sinks a row for every kBackdropLiftUnits the
// eye rises above that.
inline constexpr auto kBackdropRow = std::int16_t{50};
inline constexpr auto kBackdropLiftUnits = 10.0F;

// The sky of the driver's view, from the window's top down to the horizon in its shades, and the
// backdrop standing on the horizon, which slides against the turn of `heading` and wraps around
// (the end of RenderRoadView, 0:dc36). The backdrop's own sky takes the shade of the row it lies
// on. `lift` is how far the eye is above its rest height, units. Draws into the selected viewport.
void DrawSkyHorizon(const ViewDescription& view, const SpriteBank& backdrop, float heading, float lift, Screen& screen);

}  // namespace hp2
