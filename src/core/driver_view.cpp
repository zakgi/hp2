#include "core/driver_view.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/cockpit.hpp"
#include "core/palette.hpp"
#include "core/road_surface.hpp"
#include "core/sky_horizon.hpp"
#include "core/view_palette.hpp"

namespace hp2 {

void DriverView::InstallPalette() {
  const auto colors = assets_.Palette(EnginePalette::kView);
  if (colors.size() == kViewSetCount * kColorRegisterCount) {
    const auto set = [colors](std::size_t index) {
      return colors.subspan(index * kColorRegisterCount, kColorRegisterCount);
    };
    auto& upper = screen_.Palette(Viewport::kUpper);
    upper.Reset();
    upper.Overlay(set(kViewSet), 0);
    upper.Overlay(set(kRoofSet), kRoofOffset);
    for (auto shade = std::size_t{0}; shade < kSkyShadeCount; ++shade) {
      upper.SetColor(static_cast<std::uint8_t>(kSkyFirstEntry + shade), set(kViewSet + shade)[kSkyColor]);
    }
    for (auto ground = std::size_t{0}; ground < kGroundSetCount; ++ground) {
      upper.Overlay(set(kFirstGroundSet + ground), kGroundFirstOffset + (ground * kColorRegisterCount));
    }
    auto& lower = screen_.Palette(Viewport::kLower);
    lower.Reset();
    lower.Overlay(set(kDashboardSet), 0);
  }
}

void DriverView::Draw(const Mission& mission) {
  const auto& player = mission.GetPlayer();
  const auto input = ViewInput{
      .position = player.position, .heading = player.GetBodyHeading(), .eye_height = kEyeHeight + player.body_lift};
  // The dashes of the edge lines move with what the car has driven the way it looks.
  const auto driven = Dot(player.position - last_position_, GetDirection(input.heading));
  travel_ = std::fmod(travel_ + driven, kDashPeriodUnits);
  last_position_ = player.position;

  screen_.EnableSplit(kDashboardRow);
  InstallPalette();
  DrawSkyHorizon(kDriverView, assets_.Bank(EngineBank::kBackdrop), input.heading, player.body_lift, screen_);
  projection_.Project(input, rows_);
  DrawRoadSurface(rows_, travel_, screen_);
  objects_.Draw(kDriverView, input, road_, assets_, screen_);
  const auto& cockpit = assets_.Bank(EngineBank::kCockpit);
  DrawHoodEdge(screen_);
  DrawRoofStrip(cockpit, screen_);
  DrawRoofText(assets_.Font(EngineFont::kLettre2), GetCell(player.position), screen_);
  // While the car moves the wheel and the hands shake: up a pixel and back, kShakeTicks each.
  const auto shaken = player.speed != 0.0F and (mission.GetTick() / kShakeTicks) % 2 == 1;
  const auto& condition = mission.GetCondition();
  screen_.SetViewport(Viewport::kLower);
  DrawDashboard(cockpit,
                DashboardInput{.steer = player.controls.steer,
                               .speed = player.speed,
                               .rpm = condition.rpm,
                               .fuel = condition.fuel,
                               .temperature = condition.temperature,
                               .shaken = shaken},
                screen_);
  screen_.SetViewport(Viewport::kUpper);
}

}  // namespace hp2
