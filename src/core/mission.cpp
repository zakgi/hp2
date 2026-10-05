#include "core/mission.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace hp2 {

namespace {

// The player's car (docs/highway.md, "Handling"); provisional values, to be tuned by feel.
constexpr auto kPlayerCar = VehicleTuning{
    .top_speed = 8000.0F,          // 400 a frame at 20 frames a second: 62 m/s
    .top_reverse_speed = 2000.0F,  // 100 a frame
    .acceleration = 2000.0F,       // 0 to top speed in 4 s
    .braking = 4000.0F,            // top speed to a stop in 2 s
    .drag = 0.2F,
    .off_road_drag = 1.0F,
    .turning_radius = 700.0F,  // about 5.5 m at full lock
    .grip = 3000.0F,           // about 2.4 g: the curves (radius 8192) at up to about 5000 units/s
};

// The criminal's car and the traffic's, each with its own top speed; provisional as the player's.
constexpr auto kComputerCar = VehicleTuning{
    .acceleration = 1500.0F,
    .braking = 4000.0F,
    .drag = 0.2F,
    .off_road_drag = 1.0F,
    .turning_radius = 700.0F,
    .grip = 3000.0F,
};

// The original counts speeds in units a frame; the port takes 20 frames a second (docs/highway.md,
// "Time").
constexpr auto kOriginalFramesPerSecond = 20.0F;

// Traffic (UpdateTrafficCar, 0:540e): one car at a time, as the original, in one of 7 color
// schemes, cruising at 200 units a frame and 32 more for each scheme after the first. A car more
// than kTrafficRange cells from the player on either axis is taken away.
constexpr auto kTrafficCars = std::size_t{1};
constexpr auto kTrafficSchemes = std::uint32_t{7};
constexpr auto kTrafficSpeed = 200.0F;
constexpr auto kTrafficSpeedStep = 32.0F;
constexpr auto kTrafficRange = 2;
constexpr auto kChaseRange = 2;

// The rules (docs/game.md, "Missions"; docs/vehicles.md, sections 7.6, 8.2, 8.4 and 8.5), the
// original's amounts a frame turned into rates.
// The bounty drains a dollar every 6 frames.
constexpr auto kBountyTicks = std::uint32_t{18};
// A pull-over arrest: the criminal's car less than this far ahead and inside the driver's view,
// which is 160 pixels to either side at a focal length of 256.
constexpr auto kArrestDistance = 1000.0F;
constexpr auto kViewSlope = 160.0F / 256.0F;
// In a station's area the car loses a fifth of its speed a frame, and stands below 5 units a frame.
constexpr auto kStationBraking = 4.5F;  // of the speed, per second
constexpr auto kStationStopSpeed = 100.0F;
// Off the road at kOffRoadSpeed or more for kOffRoadSeconds, a tire gives out (CheckOffRoad,
// 0:3fa8). The original's speed is 100 units a frame, 2000 units a second here, and its time 300
// frames, the 15 seconds kept. The speed is lower than the original's because the desert's drag
// holds a car at full throttle just under 2000.
constexpr auto kOffRoadSpeed = 1500.0F;
constexpr auto kOffRoadSeconds = 15.0F;
// The car on a lost tire, and both cars once the mission is over, slow by 4 units a frame each
// frame. The crash costs the car's speed in damage each frame, of 10000 in all, shakes the body
// by an eighth of the speed, a frame up and a frame down, and turns the car four times as hard.
constexpr auto kCoastBraking = 1600.0F;  // units per second squared
constexpr auto kDamageBudget = 10000.0F;
constexpr auto kCrashShake = 1.0F / (8.0F * kOriginalFramesPerSecond);
constexpr auto kCrashSteer = 4.0F;
constexpr auto kShakeTicks = std::uint32_t{3};

// Where the car stands after a station (0:328e), as the original's table gives it.
struct StationExit {
  std::int16_t east;
  std::int16_t north;
  std::int16_t heading;
};
// North of a north-south station's driveway for a car that stopped in the cell's south half, south
// of it otherwise; east of an east-west station's for one that stopped in the west half, west
// otherwise. Each on the ramp back to the road, facing it.
constexpr auto kNorthExit = StationExit{.east = 0x2300, .north = 0x3500, .heading = 0x10e};
constexpr auto kSouthExit = StationExit{.east = 0x2300, .north = 0x0b00, .heading = 0x1c2};
constexpr auto kEastExit = StationExit{.east = 0x3500, .north = 0x1d00, .heading = 0x5a};
constexpr auto kWestExit = StationExit{.east = 0x0b00, .north = 0x1d00, .heading = 0x10e};

// `speed` brought toward a stop by `amount`.
float Slow(float speed, float amount) {
  return speed > 0.0F ? std::max(0.0F, speed - amount) : std::min(0.0F, speed + amount);
}

// Contacts (docs/highway.md, "Contacts"; UpdateImpacts, 0:4068). A car slower than kContactSpeed,
// 5 units a frame, takes nothing from what it touches. Stones cost the car's speed a frame in
// damage, a cactus or a sign twice and another car four times that, once; a bush as much as stones
// for every frame the car is in it, and here it drags the car as well.
constexpr auto kContactSpeed = 100.0F;
constexpr auto kStoneDamage = 1.0F / (kOriginalFramesPerSecond * kDamageBudget);  // per unit per second
constexpr auto kSolidDamage = 2.0F * kStoneDamage;
constexpr auto kCarDamage = 4.0F * kStoneDamage;
constexpr auto kBushDrag = 1.0F;  // of the speed, per second
// A contact between cars takes 20 dollars off the bounty for each of the two, and 20 more when the
// other car is the criminal's, unless the mission is a roadblock (CheckArrest, 0:4f14). The screen
// flashes red for two frames.
constexpr auto kContactPenalty = std::int32_t{20};
constexpr auto kFlashSeconds = 2.0F / kOriginalFramesPerSecond;

// What a scenery object is to a car that runs into it, the worst last.
enum class Obstacle : std::uint8_t { kNone, kBush, kStones, kSolid };

Obstacle GetObstacle(std::uint16_t type) {
  constexpr auto kCactus = std::uint16_t{1};
  constexpr auto kFirstStone = std::uint16_t{3};
  constexpr auto kBush = std::uint16_t{7};
  constexpr auto kLastSign = std::uint16_t{11};
  auto obstacle = Obstacle::kNone;
  if (type == kCactus or (type > kBush and type <= kLastSign)) {
    obstacle = Obstacle::kSolid;
  } else if (type >= kFirstStone and type < kBush) {
    obstacle = Obstacle::kStones;
  } else if (type == kBush) {
    obstacle = Obstacle::kBush;
  }
  return obstacle;
}

struct Touch {
  Obstacle obstacle{Obstacle::kNone};
  bool on_right{};
};

// The worst of the scenery under the half of `car`'s hit box that leads the way it moves.
Touch FindTouch(const Vehicle& car, const Road& road) {
  auto touch = Touch{};
  const auto reach = WorldPoint{.x = kHitBoxHalfLength, .y = kHitBoxHalfLength};
  const auto first = GetCell(car.position - reach);
  const auto last = GetCell(car.position + reach);
  for (auto cell_y = first.y; cell_y <= last.y; ++cell_y) {
    for (auto cell_x = first.x; cell_x <= last.x; ++cell_x) {
      const auto cell = Cell{.x = cell_x, .y = cell_y};
      const auto origin = GetCellOrigin(cell);
      for (const auto& placed : road.GetScenery(cell)) {
        const auto point = GetBodyPoint(
            car, origin + WorldPoint{.x = static_cast<float>(placed.x), .y = static_cast<float>(placed.y)});
        const auto obstacle = GetObstacle(placed.type);
        if (obstacle > touch.obstacle and std::abs(point.right) < kHitBoxHalfWidth and
            std::abs(point.ahead) < kHitBoxHalfLength and point.ahead * car.speed > 0.0F) {
          touch = Touch{.obstacle = obstacle, .on_right = point.right >= 0.0F};
        }
      }
    }
  }
  return touch;
}

// What running into the scenery did to a car.
struct Hit {
  float damage{};  // of the damage budget
  bool hard{};     // into something solid
};

// Starts what the scenery under `car` does to it: a bump over stones, a spin off a cactus or a
// sign, the drag of a bush. A car in the middle of a bump or a spin takes no other.
Hit HitScenery(Vehicle& car, const Road& road, float seconds) {
  auto hit = Hit{};
  const auto speed = std::abs(car.speed);
  if (speed >= kContactSpeed) {
    const auto touch = FindTouch(car, road);
    if (touch.obstacle == Obstacle::kBush) {
      hit.damage = speed * kStoneDamage * kOriginalFramesPerSecond * seconds;
      car.speed -= car.speed * std::min(1.0F, kBushDrag * seconds);
    } else if (touch.obstacle == Obstacle::kStones and car.impact.kind == ImpactKind::kNone) {
      hit.damage = speed * kStoneDamage;
      StartBump(car);
    } else if (touch.obstacle == Obstacle::kSolid and car.impact.kind == ImpactKind::kNone) {
      hit = Hit{.damage = speed * kSolidDamage, .hard = true};
      StartSpin(car, touch.on_right);
    }
  }
  return hit;
}

// Shots (docs/vehicles.md, sections 10.2 and 10.3).
//
// The player's gun fires once a frame of the original's while the trigger is held. Its sight lies
// 10 rows under the horizon at a focal length of 256, half a row lower for each unit the body is
// up, and strays up to 15 columns either way at 400 units a frame (0:2f08). The original hits the
// car whose picture the sight is on, at one of its 6 largest sizes; here the sight's line has to
// meet the car before the ground, no higher than the car is tall.
constexpr auto kShotTicks = std::uint32_t{3};
constexpr auto kSightDrop = 10.0F / 256.0F;                            // radians
constexpr auto kSightBob = 0.5F / 256.0F;                              // radians per unit of lift
constexpr auto kSightStray = 15.0F / (256.0F * kPlayerCar.top_speed);  // radians per unit per second
constexpr auto kEyeHeight = 100.0F;                                    // cameraHeight, 1:2368
constexpr auto kCarHeight = 170.0F;
// Five hits arrest the criminal in a shooting mission. In the others the original takes 20 dollars
// every frame from the fifth hit on; here each hit takes 20. A hit on a traffic car takes 40.
constexpr auto kArrestHits = std::uint8_t{5};
constexpr auto kTrafficHitPenalty = std::int32_t{40};

// The criminal fires every 6 frames, straight along its way toward the side the player is on, as
// far as 4000 units. The bullet marks the windshield where its line crosses a plane 350 units ahead
// of the eye, inside the view; a hole within 20 pixels of the middle of the view kills.
constexpr auto kTargetShotTicks = std::uint32_t{18};
constexpr auto kBulletRange = 4000.0F;
constexpr auto kWindshieldDepth = 350.0F;
constexpr auto kDeadlySlope = 20.0F / 256.0F;

// The part of a ray inside something, by the distances along it.
struct RaySpan {
  float enter{};
  float leave{std::numeric_limits<float>::max()};
};

// `span` cut to where a ray from `position` moving by `direction` a unit lies within `half` of 0.
RaySpan ClipToSlab(RaySpan span, float position, float direction, float half) {
  auto result = span;
  if (direction != 0.0F) {
    const auto first = (-half - position) / direction;
    const auto second = (half - position) / direction;
    result.enter = std::max(span.enter, std::min(first, second));
    result.leave = std::min(span.leave, std::max(first, second));
  } else if (std::abs(position) > half) {
    result.leave = -1.0F;
  }
  return result;
}

// How far along the ray from `origin` along `direction` it enters `car`'s hit box, if it does.
std::optional<float> FindRayHit(const Vehicle& car, WorldPoint origin, WorldPoint direction) {
  const auto start = GetBodyPoint(car, origin);
  const auto forward = GetDirection(car.GetBodyHeading());
  auto span = ClipToSlab(RaySpan{}, start.ahead, Dot(direction, forward), kHitBoxHalfLength);
  span = ClipToSlab(span, start.right, -Cross(forward, direction), kHitBoxHalfWidth);
  return span.enter <= span.leave ? std::optional{span.enter} : std::nullopt;
}

// Whether the middle of `second` lies within a hit box's width and length of the middle of `first`,
// across and along the body of `first`: where two hit boxes side by side or nose to tail meet.
bool Touches(const Vehicle& first, const Vehicle& second) {
  const auto point = GetBodyPoint(first, second.position);
  return std::abs(point.right) < 2.0F * kHitBoxHalfWidth and std::abs(point.ahead) < 2.0F * kHitBoxHalfLength;
}

// A car's place at the start of a mission, as the original's tables give it: a cell, the position
// inside it and a heading in half degrees.
struct StartPlace {
  Cell cell;
  std::int16_t east;
  std::int16_t north;
  std::int16_t heading;
};

// The player's three starts (0:b76e). The original picks one with two bits of the beam position
// and gives the fourth value to the first start; here each is as likely.
constexpr auto kPlayerStarts = std::to_array<StartPlace>({
    {.cell = {.x = 11, .y = 15}, .east = 0x07d0, .north = 0x2000, .heading = 0},
    {.cell = {.x = 24, .y = 11}, .east = 0x2000, .north = 0x07d0, .heading = 0xb4},
    {.cell = {.x = 25, .y = 28}, .east = 0x2000, .north = 0x3830, .heading = 0x21c},
});

// The criminal's 16 starts (1:236e), all crossroads, at the same place in the cell, facing south.
constexpr auto kTargetStartCells = std::to_array<Cell>({
    {.x = 25, .y = 5},
    {.x = 3, .y = 6},
    {.x = 12, .y = 6},
    {.x = 27, .y = 6},
    {.x = 30, .y = 7},
    {.x = 16, .y = 8},
    {.x = 33, .y = 14},
    {.x = 30, .y = 20},
    {.x = 21, .y = 21},
    {.x = 12, .y = 24},
    {.x = 16, .y = 26},
    {.x = 35, .y = 27},
    {.x = 10, .y = 29},
    {.x = 31, .y = 30},
    {.x = 30, .y = 32},
    {.x = 14, .y = 35},
});
constexpr auto kTargetStartEast = std::int16_t{0x2000};
constexpr auto kTargetStartNorth = std::int16_t{0x3830};
constexpr auto kTargetStartHeading = std::int16_t{0x21c};

Vehicle PlaceVehicle(Cell cell, std::int16_t east, std::int16_t north, std::int16_t heading) {
  return Vehicle{
      .position = GetCellOrigin(cell) + WorldPoint{.x = static_cast<float>(east), .y = static_cast<float>(north)},
      .heading = FromHalfDegrees(heading)};
}

}  // namespace

bool IsInArrestReach(const Vehicle& player, const Vehicle& target) {
  const auto apart = target.position - player.position;
  const auto way = GetDirection(player.heading);
  const auto ahead = Dot(apart, way);
  return GetCell(target.position) == GetCell(player.position) and ahead > 0.0F and ahead < kArrestDistance and
         std::abs(Cross(way, apart)) < ahead * kViewSlope and
         std::abs(WrapAngle(target.heading - player.heading)) < kFullTurn / 4.0F;
}

void Mission::Start(const MissionType& mission, const AiPolicies& policies, std::uint64_t seed) {
  random_ = Random{seed};
  policies_ = policies;
  const auto& player = kPlayerStarts[random_.Below(static_cast<std::uint32_t>(kPlayerStarts.size()))];
  player_ = PlaceVehicle(player.cell, player.east, player.north, player.heading);
  const auto target = kTargetStartCells[random_.Below(static_cast<std::uint32_t>(kTargetStartCells.size()))];
  target_ = PlaceVehicle(target, kTargetStartEast, kTargetStartNorth, kTargetStartHeading);
  traffic_count_ = 0;
  condition_ = CarCondition{};
  progress_ = MissionProgress{.mission = mission, .bounty = static_cast<std::int32_t>(mission.bounty)};
  station_.reset();
  rammed_standing_ = false;
  shot_dead_ = false;
  tick_ = 0;
  target_distances_.Build(road_, GetCell(target_.position));
  player_distances_.Build(road_, GetCell(player_.position));
  target_driver_.cruise_speed = static_cast<float>(mission.target_max_speed) * kOriginalFramesPerSecond;
  if (not SendTarget(progress_.robbed)) {
    // No station to go to: the criminal stays where it is.
    target_driver_.plan = AiDriver::Plan::kStopped;
  }
  PlanLanes(target_driver_, road_, target_.position, true);
}

MissionEvents Mission::Step(const PlayerCommands& commands) {
  auto events = MissionEvents{};
  progress_.flash_seconds = std::max(progress_.flash_seconds - kTickSeconds, 0.0F);
  StepPlayer(commands, events);
  StepTarget(events);
  StepTraffic();
  ResolveContacts(events);
  ResolveShots(commands, events);
  ApplyRules(events);
  ++tick_;
  return events;
}

void Mission::LeaveStation(float fuel, std::uint8_t tires) {
  const auto cell = GetCell(player_.position);
  if (road_.GetStationIndex(cell)) {
    const auto stopped = player_.position - GetCellOrigin(cell);
    auto exit = stopped.x < kCellUnits / 2.0F ? kEastExit : kWestExit;
    if (HasSide(road_.GetExits(cell), Side::kNorth)) {
      exit = stopped.y < kCellUnits / 2.0F ? kNorthExit : kSouthExit;
    }
    player_ = PlaceVehicle(cell, exit.east, exit.north, exit.heading);
  }
  // The stop cools the engine as well (0:331e).
  condition_.fuel = fuel;
  condition_.tires = tires;
  condition_.temperature = 0.0F;
  condition_.off_road_seconds = 0.0F;
  condition_.off_road = false;
  station_.reset();
}

void Mission::StepPlayer(const PlayerCommands& commands, MissionEvents& events) {
  progress_.siren = progress_.siren != commands.toggle_siren;
  progress_.aiming = progress_.aiming != commands.toggle_aim;
  const auto over = progress_.end_reason.has_value();
  const auto crashing = player_.impact.kind == ImpactKind::kCrash;
  const auto in_station =
      not over and player_.impact.kind == ImpactKind::kNone and road_.IsInStationArea(player_.position);
  player_.controls = commands.controls;
  if (over or crashing or in_station) {
    // The car slows by itself: no throttle.
    player_.controls.throttle = 0.0F;
  }
  if (crashing) {
    player_.controls.steer = std::clamp(commands.controls.steer * kCrashSteer, -1.0F, 1.0F);
  }
  Drive(player_, kPlayerCar, road_, kTickSeconds);
  if (over or crashing) {
    player_.speed = Slow(player_.speed, kCoastBraking * kTickSeconds);
  } else if (in_station) {
    player_.speed -= player_.speed * std::min(1.0F, kStationBraking * kTickSeconds);
    if (std::abs(player_.speed) < kStationStopSpeed) {
      player_.speed = 0.0F;
      station_ = road_.GetStationIndex(GetCell(player_.position));
      events.set(std::to_underlying(MissionEvent::kStationStop));
    }
  }
  if (crashing) {
    StepCrash(events);
  } else if (not over) {
    TrackOffRoad(events);
  }
  condition_.Update(player_, kTickSeconds);
}

void Mission::StepCrash(MissionEvents& events) {
  const auto speed = std::abs(player_.speed);
  player_.impact.seconds += kTickSeconds;
  condition_.damage = std::min(1.0F, condition_.damage + (speed * kTickSeconds / kDamageBudget));
  player_.body_lift = speed * kCrashShake * ((tick_ / kShakeTicks) % 2 == 0 ? 1.0F : -1.0F);
  if (speed == 0.0F) {
    player_.impact = Impact{};
    // With a tire left the mission goes on after the spin-out picture; without, the rules end it.
    if (condition_.tires > 0 and not progress_.end_reason) {
      events.set(std::to_underlying(MissionEvent::kSpinOut));
    }
  }
}

void Mission::TrackOffRoad(MissionEvents& events) {
  const auto off_road = not road_.IsOnRoad(player_.position);
  if (off_road and std::abs(player_.speed) >= kOffRoadSpeed) {
    condition_.off_road_seconds += kTickSeconds;
    if (condition_.off_road_seconds >= kOffRoadSeconds) {
      condition_.off_road_seconds = 0.0F;
      condition_.tires = static_cast<std::uint8_t>(std::max(condition_.tires - 1, 0));
      player_.impact = Impact{.kind = ImpactKind::kCrash};
      events.set(std::to_underlying(MissionEvent::kCrash));
    }
  } else if (not off_road and condition_.off_road) {
    condition_.off_road_seconds /= 2.0F;
  }
  condition_.off_road = off_road;
}

Vehicle& Mission::GetCar(std::size_t index) {
  if (index == 0) {
    return player_;
  }
  return index == 1 ? target_ : traffic_[index - 2];
}

void Mission::ResolveContacts(MissionEvents& events) {
  // The scenery: every car runs into it, the player's takes the damage.
  const auto hit = HitScenery(player_, road_, kTickSeconds);
  condition_.damage = std::min(1.0F, condition_.damage + hit.damage);
  if (hit.hard) {
    events.set(std::to_underlying(MissionEvent::kCrash));
  }
  HitScenery(target_, road_, kTickSeconds);
  for (auto& car : std::span{traffic_}.first(traffic_count_)) {
    HitScenery(car, road_, kTickSeconds);
  }
  // The cars, pair by pair: a contact counts when it starts, for two cars not both against
  // another already.
  const auto count = 2 + traffic_count_;
  auto touching = std::array<bool, 2 + kMaxTraffic>{};
  for (auto first = std::size_t{0}; first < count; ++first) {
    for (auto second = first + 1; second < count; ++second) {
      auto& car = GetCar(first);
      auto& other = GetCar(second);
      if (Touches(car, other) or Touches(other, car)) {
        touching[first] = true;
        touching[second] = true;
        if (not(car.touching and other.touching)) {
          HitCars(car, other, first == 0, second == 1, events);
        }
      }
    }
  }
  for (auto index = std::size_t{0}; index < count; ++index) {
    GetCar(index).touching = touching[index];
  }
}

void Mission::HitCars(Vehicle& first, Vehicle& second, bool first_is_player, bool second_is_target,
                      MissionEvents& events) {
  const auto first_speed = std::abs(first.speed);
  const auto second_speed = std::abs(second.speed);
  // The slower car is knocked on at half the speed of the faster.
  if (first_speed < second_speed) {
    first.speed = second_speed / 2.0F;
  } else {
    second.speed = first_speed / 2.0F;
  }
  // Each spins away from the side the other is on.
  if (first.impact.kind == ImpactKind::kNone) {
    StartSpin(first, GetBodyPoint(first, second.position).right >= 0.0F);
  }
  if (second.impact.kind == ImpactKind::kNone) {
    StartSpin(second, GetBodyPoint(second, first.position).right >= 0.0F);
  }
  if (first_is_player) {
    condition_.damage = std::min(1.0F, condition_.damage + (std::abs(first.speed) * kCarDamage));
    events.set(std::to_underlying(MissionEvent::kCrash));
    progress_.flash_seconds = kFlashSeconds;
    auto penalty = 2 * kContactPenalty;
    if (second_is_target and progress_.mission.arrest == ArrestMethod::kRoadblock) {
      rammed_standing_ = rammed_standing_ or first_speed < kContactSpeed;
    } else if (second_is_target) {
      penalty += kContactPenalty;
    }
    progress_.bounty = std::max(progress_.bounty - penalty, std::int32_t{0});
  }
}

void Mission::ResolveShots(const PlayerCommands& commands, MissionEvents& events) {
  const auto over = progress_.end_reason.has_value();
  if (progress_.aiming) {
    const auto shot_tick = tick_ % kShotTicks == 0;
    if (shot_tick) {
      const auto stray = (2.0F * random_.GetUnit()) - 1.0F;
      progress_.sight.bearing = stray * std::abs(player_.speed) * kSightStray;
    }
    progress_.sight.elevation = -(kSightDrop + (player_.body_lift * kSightBob));
    if (commands.firing and shot_tick and not over) {
      FirePlayerShot(events);
    }
  }
  if (not progress_.aiming or not commands.firing) {
    progress_.shot_hit = false;
  }
  if (tick_ % kTargetShotTicks == 0 and not over) {
    const auto context = GetPolicyContext(progress_.robbed);
    if (std::visit([&](const auto& policy) { return policy.Shoot(context, random_); }, policies_.reaction)) {
      FireTargetShot(events);
    }
  }
}

void Mission::FirePlayerShot(MissionEvents& events) {
  events.set(std::to_underlying(MissionEvent::kShot));
  // The nearest car the sight's line meets while it is over the ground and no higher than a car.
  const auto direction = GetDirection(player_.GetBodyHeading() + progress_.sight.bearing);
  auto nearest = std::numeric_limits<float>::max();
  auto hit = std::optional<std::size_t>{};
  for (auto index = std::size_t{1}; index < 2 + traffic_count_; ++index) {
    if (const auto distance = FindRayHit(GetCar(index), player_.position, direction)) {
      const auto height = kEyeHeight + player_.body_lift + (*distance * progress_.sight.elevation);
      if (*distance < nearest and height >= 0.0F and height <= kCarHeight) {
        nearest = *distance;
        hit = index;
      }
    }
  }
  progress_.shot_hit = hit.has_value();
  if (hit) {
    auto& car = GetCar(*hit);
    auto penalty = std::int32_t{0};
    auto spins = true;
    if (*hit != 1) {
      events.set(std::to_underlying(MissionEvent::kTrafficHit));
      penalty = kTrafficHitPenalty;
    } else {
      events.set(std::to_underlying(MissionEvent::kTargetHit));
      progress_.target_hits = static_cast<std::uint8_t>(std::min(progress_.target_hits + 1, int{kArrestHits}));
      const auto shooting = progress_.mission.arrest == ArrestMethod::kShoot;
      penalty = shooting ? 0 : kContactPenalty;
      // The hit that makes the arrest spins the criminal's car out.
      spins = shooting and progress_.target_hits == kArrestHits;
    }
    if (spins and car.impact.kind == ImpactKind::kNone) {
      car.impact = Impact{.kind = ImpactKind::kSpin, .spin = 1.0F};
    }
    progress_.bounty = std::max(progress_.bounty - penalty, std::int32_t{0});
  }
}

void Mission::FireTargetShot(MissionEvents& events) {
  // The player along the criminal's way: ahead of it or behind, within a bullet's range.
  const auto way = GetDirection(target_.heading);
  const auto along = Dot(player_.position - target_.position, way);
  const auto fire = way * (along >= 0.0F ? 1.0F : -1.0F);
  // The bullet's line as the player's eye sees it, and how far along it crosses the windshield.
  const auto forward = GetDirection(player_.GetBodyHeading());
  const auto start = GetBodyPoint(player_, target_.position);
  const auto closing = Dot(fire, forward);
  const auto travel = closing != 0.0F ? (kWindshieldDepth - start.ahead) / closing : -1.0F;
  if (std::abs(along) < kBulletRange and travel >= 0.0F and travel <= kBulletRange) {
    const auto right = start.right - (travel * Cross(forward, fire));
    if (std::abs(right) < kWindshieldDepth * kViewSlope) {
      events.set(std::to_underlying(MissionEvent::kWindshieldHit));
      const auto hole = ViewDirection{.bearing = std::atan2(-right, kWindshieldDepth),
                                      .elevation = -2.0F * target_.body_lift / kWindshieldDepth};
      const auto deadly = std::abs(right) < kWindshieldDepth * kDeadlySlope and std::abs(hole.elevation) < kDeadlySlope;
      if (deadly or progress_.hole_count == MissionProgress::kMaxWindshieldHoles) {
        shot_dead_ = true;
      } else {
        progress_.holes[progress_.hole_count] = hole;
        ++progress_.hole_count;
      }
    }
  }
}

bool Mission::IsArrested() const {
  auto arrested = false;
  switch (progress_.mission.arrest) {
    case ArrestMethod::kPullOver:
      arrested = progress_.siren and IsInArrestReach(player_, target_);
      break;
    case ArrestMethod::kRoadblock:
      arrested = rammed_standing_;
      break;
    case ArrestMethod::kShoot:
      arrested = progress_.target_hits >= kArrestHits;
      break;
  }
  return arrested;
}

void Mission::ApplyRules(MissionEvents& events) {
  if (not progress_.end_reason) {
    if (tick_ % kBountyTicks == kBountyTicks - 1) {
      progress_.bounty = std::max(progress_.bounty - 1, std::int32_t{0});
    }
    // The first that holds, in the original's order, is the ending shown (CheckMissionEnd, 0:6008).
    auto reason = std::optional<EndReason>{};
    if (condition_.fuel <= 0.0F) {
      reason = EndReason::kOutOfFuel;
    } else if (progress_.robbed.count() >= road_.GetStations().size()) {
      reason = EndReason::kStationsRobbed;
    } else if (condition_.temperature >= 1.0F) {
      reason = EndReason::kOverheated;
    } else if (condition_.damage >= 1.0F) {
      reason = EndReason::kWrecked;
    } else if (condition_.tires == 0) {
      reason = EndReason::kTiresGone;
    } else if (shot_dead_) {
      reason = EndReason::kShot;
    } else if (IsArrested()) {
      reason = EndReason::kArrest;
    } else if (progress_.bounty <= 0) {
      reason = EndReason::kBountyGone;
    }
    if (reason) {
      progress_.end_reason = reason;
      events.set(std::to_underlying(MissionEvent::kMissionOver));
    }
  }
}

void Mission::StepTarget([[maybe_unused]] MissionEvents& events) {
  auto& driver = target_driver_;
  driver.plan_seconds += kTickSeconds;
  if (GetCell(target_.position) != driver.cell and not progress_.end_reason) {
    // A new cell: the moment to give up the station it was heading for, if the player is closing in.
    const auto context = GetPolicyContext(progress_.robbed);
    if (driver.plan == AiDriver::Plan::kCruise and
        std::visit([&](const auto& policy) { return policy.Flee(context, random_); }, policies_.reaction)) {
      auto skipped = progress_.robbed;
      if (const auto station = road_.GetStationIndex(driver.route.GetGoal())) {
        skipped.set(*station);
      }
      SendTarget(skipped);
    }
    PlanLanes(driver, road_, target_.position, true);
  }
  if (progress_.end_reason) {
    // The mission is over: the criminal's car stops too, where it is.
    driver.plan = AiDriver::Plan::kStopped;
  } else if (driver.plan == AiDriver::Plan::kCruise and driver.cell == driver.route.GetGoal()) {
    // In the station's cell, on its driveway.
    driver.plan = AiDriver::Plan::kPullIn;
    driver.plan_seconds = 0.0F;
  } else if (driver.plan == AiDriver::Plan::kPullIn and target_.speed == 0.0F and
             road_.IsInStationArea(target_.position)) {
    driver.plan = AiDriver::Plan::kStopped;
    driver.plan_seconds = 0.0F;
  } else if (driver.plan == AiDriver::Plan::kStopped) {
    RobStation();
  }
  auto tuning = kComputerCar;
  tuning.top_speed = driver.cruise_speed;
  target_.controls = FollowLane(driver, target_, tuning);
  Drive(target_, tuning, road_, kTickSeconds);
  if (progress_.end_reason and target_.impact.kind == ImpactKind::kNone) {
    // Once the mission is over the criminal's car takes the player's speed, so the two roll to a
    // stop together (CheckMissionEnd, 0:6008).
    target_.speed = std::abs(player_.speed);
  }
}

void Mission::StepTraffic() {
  const auto player_cell = GetCell(player_.position);
  // With the player within two cells of the criminal the road is kept clear for the two of them:
  // no new traffic, and a traffic car goes once the player cannot see it. The original takes it
  // away at once unless it is in the list of drawn objects but off the screen, in which case both
  // computer cars brake instead (0:540e); what that was for is not known.
  const auto target_cell = GetCell(target_.position);
  const auto chasing =
      std::abs(target_cell.x - player_cell.x) <= kChaseRange and std::abs(target_cell.y - player_cell.y) <= kChaseRange;
  auto index = std::size_t{0};
  while (index < traffic_count_) {
    auto& car = traffic_[index];
    auto& driver = traffic_drivers_[index];
    const auto cell = GetCell(car.position);
    const auto seen = GetBodyPoint(player_, car.position);
    const auto in_view = seen.ahead > 0.0F and seen.ahead < kCellUnits and
                         std::abs(seen.right) < (seen.ahead * kViewSlope) + (2.0F * kHitBoxHalfLength);
    if (std::abs(cell.x - player_cell.x) > kTrafficRange or std::abs(cell.y - player_cell.y) > kTrafficRange or
        (chasing and not in_view)) {
      // Out of range: gone, and the last car takes its place in the list.
      --traffic_count_;
      if (index != traffic_count_) {
        car = traffic_[traffic_count_];
        driver = traffic_drivers_[traffic_count_];
      }
    } else {
      if (cell != driver.cell) {
        if (cell == driver.route.GetGoal()) {
          const auto context = GetPolicyContext(progress_.robbed);
          driver.route.Build(road_,
                             std::visit([&](const auto& policy) { return policy.ChooseGoal(context, car, random_); },
                                        policies_.traffic));
        }
        PlanLanes(driver, road_, car.position, false);
      }
      auto tuning = kComputerCar;
      tuning.top_speed = driver.cruise_speed;
      car.controls = FollowLane(driver, car, tuning);
      Drive(car, tuning, road_, kTickSeconds);
      ++index;
    }
  }
  if (traffic_count_ < kTrafficCars and not chasing) {
    SpawnTraffic();
  }
}

void Mission::SpawnTraffic() {
  const auto context = GetPolicyContext(progress_.robbed);
  const auto spawn = std::visit([&](const auto& policy) { return policy.Spawn(context, random_); }, policies_.traffic);
  if (spawn) {
    auto& driver = traffic_drivers_[traffic_count_];
    driver.plan = AiDriver::Plan::kCruise;
    driver.plan_seconds = 0.0F;
    driver.route.Build(road_, spawn->goal);
    // Planned from just inside the side the car comes in by, which makes that side its entry.
    const auto half = kCellUnits / 2.0F;
    PlanLanes(
        driver, road_,
        GetCellOrigin(spawn->cell) + WorldPoint{.x = half, .y = half} + (GetOutward(spawn->entry) * (half - 1.0F)),
        false);
    if (const auto& lane = driver.lanes[0]; lane.GetLength() > 0.0F) {
      const auto scheme = 1U + random_.Below(kTrafficSchemes);
      driver.cruise_speed =
          (kTrafficSpeed + (kTrafficSpeedStep * static_cast<float>(scheme - 1U))) * kOriginalFramesPerSecond;
      traffic_[traffic_count_] = Vehicle{.position = lane.GetPoint(1.0F),
                                         .heading = lane.GetHeading(0.0F),
                                         .speed = driver.cruise_speed,
                                         .color_scheme = static_cast<std::uint8_t>(scheme)};
      ++traffic_count_;
    }
  }
}

PolicyContext Mission::GetPolicyContext(const std::bitset<kStationCount>& robbed) {
  if (const auto cell = GetCell(target_.position); cell != target_distances_.GetGoal()) {
    target_distances_.Build(road_, cell);
  }
  if (const auto cell = GetCell(player_.position); cell != player_distances_.GetGoal()) {
    player_distances_.Build(road_, cell);
  }
  return PolicyContext{.road = road_,
                       .arrest = progress_.mission.arrest,
                       .player = player_,
                       .target = target_,
                       .robbed = robbed,
                       .target_distances = target_distances_,
                       .player_distances = player_distances_};
}

bool Mission::SendTarget(const std::bitset<kStationCount>& skipped) {
  const auto context = GetPolicyContext(skipped);
  const auto station =
      std::visit([&](const auto& policy) { return policy.ChooseStation(context, random_); }, policies_.station);
  if (station) {
    target_driver_.route.Build(road_, road_.GetStations()[*station]);
    target_driver_.plan = AiDriver::Plan::kCruise;
    target_driver_.plan_seconds = 0.0F;
  }
  return station.has_value();
}

void Mission::RobStation() {
  auto skipped = progress_.robbed;
  if (const auto station = road_.GetStationIndex(target_driver_.cell)) {
    const auto context = GetPolicyContext(progress_.robbed);
    if (std::visit([&](const auto& policy) { return policy.Rob(context, random_); }, policies_.reaction)) {
      progress_.robbed.set(*station);
    }
    skipped = progress_.robbed;
    skipped.set(*station);
  }
  // On to another station, along the rest of the driveway; with none left the criminal stays.
  if (SendTarget(skipped)) {
    PlanNextLane(target_driver_, road_, true);
  }
}

}  // namespace hp2
