#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/engine_assets.hpp"
#include "core/road.hpp"
#include "core/road_projection.hpp"
#include "core/screen.hpp"
#include "core/world.hpp"

namespace hp2 {

// What stands beside the road in the driver's view: the cacti, bushes, stones and signs of the cells
// in view (COOR_OBJ.BIN), drawn far to near, each at the size of its depth and in the colors of the
// ground's haze where it stands, and the cars among them (BuildViewObjects 0:2452, DrawObjects
// 0:26e8).
//
// The original takes the objects of the one or two cells whose road it draws and leaves out those
// further than 8500 units; here they come from every cell out to kDepth. It shows one car, the
// criminal's before the traffic's; here every car in view is drawn.
class RoadsideObjects {
 public:
  // Nearer than this an object is not drawn, as in the original (CullObjects, 0:23e2).
  static constexpr auto kNearestDepth = 300.0F;
  // How far ahead objects are drawn, units.
  static constexpr auto kDepth = kCellUnits;
  // The objects in view at once; the ones found past these are left out.
  static constexpr auto kMaxObjects = std::size_t{160};
  // The cars drawn at once.
  static constexpr auto kMaxCars = std::size_t{5};
  // The row a car's wheels stand on at the lowest, so that one close ahead shows over the hood
  // (ClampSpriteY, 0:2dbc).
  static constexpr auto kLowestCarRow = std::int16_t{138};

  // A car to draw: where it is, the way its body points, how far the body rides above its rest
  // height, and the palette offset of its 8 colors.
  struct Car {
    WorldPoint position;
    float heading{};
    float lift{};
    std::uint8_t index_offset{};
  };

  // Draws what the eye of `input` sees into the selected viewport.
  void Draw(const ViewDescription& view, const ViewInput& input, const Road& road, std::span<const Car> cars,
            const EngineAssets& assets, Screen& screen);

 private:
  // A car as the eye sees it.
  struct ViewCar {
    float depth{};
    float offset{};
    float lift{};
    std::uint8_t view{};  // which of the 24 sides of the car shows
    std::uint8_t index_offset{};
  };

  // Fills cars_ with the cars in view, the farthest first.
  void CollectCars(const ViewDescription& view, const ViewInput& input, std::span<const Car> cars);
  static void DrawCar(const ViewDescription& view, const ViewInput& input, const ViewCar& car,
                      const EngineAssets& assets, Screen& screen);

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
  std::array<ViewCar, kMaxCars> cars_{};
  std::size_t car_count_{};
};

}  // namespace hp2
