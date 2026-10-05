#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/engine_assets.hpp"
#include "core/road.hpp"
#include "core/road_projection.hpp"
#include "core/screen.hpp"
#include "core/world.hpp"

namespace hp2 {

// What stands beside the road in the driver's view: the cacti, bushes, stones and signs of the cells
// in view (COOR_OBJ.BIN), drawn far to near, each at the size of its depth and in the colors of the
// ground's haze where it stands (BuildViewObjects 0:2452, DrawObjects 0:26e8).
//
// The original takes the objects of the one or two cells whose road it draws and leaves out those
// further than 8500 units; here they come from every cell out to kDepth.
class RoadsideObjects {
 public:
  // Nearer than this an object is not drawn, as in the original (CullObjects, 0:23e2).
  static constexpr auto kNearestDepth = 300.0F;
  // How far ahead objects are drawn, units.
  static constexpr auto kDepth = kCellUnits;
  // The objects in view at once; the ones found past these are left out.
  static constexpr auto kMaxObjects = std::size_t{160};

  // Draws what the eye of `input` sees into the selected viewport.
  void Draw(const ViewDescription& view, const ViewInput& input, const Road& road, const EngineAssets& assets,
            Screen& screen);

 private:
  // An object as the eye sees it.
  struct ViewObject {
    float depth{};   // ahead of the eye, units
    float offset{};  // to its right, units
    // As PlacedObject's.
    std::uint16_t type{};
    std::uint16_t extra{};
  };

  // Fills objects_ with what stands in view, the farthest first.
  void Collect(const ViewDescription& view, const ViewInput& input, const Road& road);

  std::array<ViewObject, kMaxObjects> objects_{};
  std::size_t count_{};
};

}  // namespace hp2
