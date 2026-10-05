#include "core/driver_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

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

void DriverView::InstallCarColors(const Mission& mission) {
  auto& upper = screen_.Palette(Viewport::kUpper);
  const auto paint = [&upper](std::size_t offset, std::size_t scheme) {
    upper.Overlay(kCarPaints[std::min(scheme, kCarPaints.size() - 1)], offset + kCarPaintFirst);
  };
  paint(0, 0);
  const auto colors = assets_.Palette(EnginePalette::kView);
  const auto traffic = mission.GetTraffic();
  if (colors.size() == kViewSetCount * kColorRegisterCount) {
    for (auto index = std::size_t{0}; index < std::min(traffic.size(), kTrafficPaintCount); ++index) {
      const auto offset = kTrafficFirstOffset + (index * kCarColorCount);
      upper.Overlay(colors.subspan(kViewSet * kColorRegisterCount, kCarColorCount), offset);
      paint(offset, traffic[index].color_scheme);
    }
  }
}

void DriverView::Draw(const Mission& mission) {
  // The criminal's car, then the traffic's, which take turns at the copies of the car colors when
  // there are more of them than copies.
  static_assert(1 + Mission::kMaxTraffic <= RoadsideObjects::kMaxCars);
  auto cars = std::array<RoadsideObjects::Car, RoadsideObjects::kMaxCars>{};
  auto car_count = std::size_t{0};
  const auto add_car = [&cars, &car_count](const Vehicle& car, std::size_t index_offset) {
    cars[car_count] = RoadsideObjects::Car{.position = car.position,
                                           .heading = car.GetBodyHeading(),
                                           .lift = car.body_lift,
                                           .index_offset = static_cast<std::uint8_t>(index_offset)};
    ++car_count;
  };
  add_car(mission.GetTarget(), 0);
  for (const auto& car : mission.GetTraffic()) {
    add_car(car, kTrafficFirstOffset + (((car_count - 1) % kTrafficPaintCount) * kCarColorCount));
  }

  const auto& player = mission.GetPlayer();
  const auto input = ViewInput{
      .position = player.position, .heading = player.GetBodyHeading(), .eye_height = kEyeHeight + player.body_lift};
  // The dashes of the edge lines move with what the car has driven the way it looks.
  const auto driven = Dot(player.position - last_position_, GetDirection(input.heading));
  travel_ = std::fmod(travel_ + driven, kDashPeriodUnits);
  last_position_ = player.position;

  const auto& progress = mission.GetProgress();
  // The left hand takes three of the original's frames to reach the gun, and as long to go back.
  const auto ticks = static_cast<float>(mission.GetTick() - last_tick_);
  last_tick_ = mission.GetTick();
  aim_ = std::clamp(aim_ + ((progress.aiming ? ticks : -ticks) / kAimTicks), 0.0F, 1.0F);

  screen_.EnableSplit(kDashboardRow);
  InstallPalette();
  InstallCarColors(mission);
  DrawSkyHorizon(kDriverView, assets_.Bank(EngineBank::kBackdrop), input.heading, player.body_lift, screen_);
  projection_.Project(input, rows_);
  DrawRoadSurface(rows_, travel_, screen_);
  objects_.Draw(kDriverView, input, road_, std::span{cars}.first(car_count), assets_, screen_);
  // On the windshield: the holes of the criminal's bullets, then the sight once the gun is up.
  const auto& gun = assets_.Bank(EngineBank::kGun);
  const auto place = [](ViewDirection direction) {
    const auto column = kDriverView.center_column - (std::tan(direction.bearing) * kDriverView.focal_length);
    const auto row = static_cast<float>(kDriverView.horizon_row) - (direction.elevation * kDriverView.focal_length);
    return Point{.x = static_cast<std::int16_t>(std::lround(column)), .y = static_cast<std::int16_t>(std::lround(row))};
  };
  for (const auto& hole : std::span{progress.holes}.first(progress.hole_count)) {
    DrawBulletHole(gun, place(hole), screen_);
  }
  if (aim_ >= 1.0F) {
    DrawSight(gun, place(progress.sight), progress.shot_hit, screen_);
  }
  const auto& cockpit = assets_.Bank(EngineBank::kCockpit);
  DrawHoodEdge(screen_);
  DrawRoofStrip(cockpit, screen_);
  DrawRoofText(assets_.Font(EngineFont::kLettre2), assets_.Font(EngineFont::kLettre1),
               RoofText{.player = GetCell(player.position),
                        .player_heading = player.heading,
                        .target = GetCell(mission.GetTarget().position),
                        .target_heading = mission.GetTarget().heading,
                        .bounty = progress.bounty,
                        .stations_robbed = static_cast<int>(progress.robbed.count())},
               screen_);
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
                               .shaken = shaken,
                               .aim = aim_},
                screen_);
  screen_.SetViewport(Viewport::kUpper);
}

}  // namespace hp2
