#pragma once

#include <cstdint>

#include "core/engine_assets.hpp"
#include "core/mission.hpp"
#include "core/road.hpp"
#include "core/road_projection.hpp"
#include "core/roadside_objects.hpp"
#include "core/screen.hpp"
#include "core/world.hpp"

namespace hp2 {

// The road as the driver sees it, the original's driving screen (docs/renderer.md): the sky and the
// backdrop on the horizon, the ground with the road, what stands beside it, then the car's own
// roof, hood and dashboard with the wheel and the hands, drawn in that order from the player's car.
class DriverView {
 public:
  // The eye's height above the ground with the car at rest, units (cameraHeight, 1:2368).
  static constexpr auto kEyeHeight = 100.0F;
  // The wheel and the hands shake at the original's pace, a pixel up or down every frame of about
  // 20 a second (provisional, as the time scale: docs/highway.md, "Time").
  static constexpr auto kShakeTicks = std::uint32_t{3};
  // The ticks the left hand takes between the wheel and the gun: three of the original's frames
  // (aimHandAngle, 20 half degrees a frame over 60).
  static constexpr auto kAimTicks = 9.0F;

  DriverView(const EngineAssets& assets, Screen& screen, const Road& road)
      : assets_(assets), screen_(screen), road_(road), projection_(kDriverView, road) {}

  // Installs the driving screen's split and palettes and draws the view of `mission`.
  void Draw(const Mission& mission);

 private:
  // Places the sets of the driving screen's colors in the two viewports (core/view_palette.hpp).
  void InstallPalette();
  // Places the paint of the criminal's car and of the traffic's (core/view_palette.hpp).
  void InstallCarColors(const Mission& mission);

  const EngineAssets& assets_;
  Screen& screen_;
  const Road& road_;
  RoadProjection projection_;
  RoadRows rows_;
  RoadsideObjects objects_;
  // Where the car was at the last draw, and how far it has driven ahead since the first, within
  // one period of the edge lines' dashes.
  WorldPoint last_position_;
  float travel_{};
  // How far the left hand has gone for the gun, 0 to 1, and the tick of the last draw.
  float aim_{};
  std::uint32_t last_tick_{};
};

}  // namespace hp2
