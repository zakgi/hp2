# Highway: the mission and its views

Status: being implemented; the headers declare the planned interfaces, and what is built so far is noted in each section. The original's behavior is the reference (`vehicles.md`, `game.md`, `renderer.md`); this file describes how the port is built and where it departs from it on purpose. Choices marked *provisional* wait for a decision.

## Parts

| Part | Owns | Knows nothing of |
|---|---|---|
| `Mission` (`src/core/mission.hpp`) | one mission: the cars, the AI drivers, the rules, the random numbers; advanced one fixed tick at a time | screen, keys, sound |
| `Road` (`road.hpp`), `world.hpp` | units, cells, angles; queries on the road map, the road shapes, the lanes and the scenery | cars |
| Cars (`vehicle.hpp`), AI (`ai.hpp`) | a car's state and how it moves; how a computer driver picks its way and follows its lane | rules |
| `Highway` component (`highway.hpp`) | keys to commands, the tick clock, events to sounds and scenes, the view shown | rules |
| Views (not sketched yet) | drawing a mission: the driver's view (`renderer.md`) and the map | rules |

Per frame:

```
KeyEvents --> Highway --PlayerCommands--> Mission::Step (whole ticks) --MissionEvents--> Highway --> sounds, Station, MissionEnd
                                          Mission (read only) --> driver's view or map --> Screen
```

The original does everything in one loop (`0:ba7c`), one step per drawn frame, and reads the drawn screen back into the game: the off-road test looks at two pixels under the hood (`0:3fa8`), the shot test at the sprite last drawn (`0:4dca`). The port's `Mission` never reads the screen, so it runs headless in tests and the views can change (field depth, resolution, the map) without touching the rules.

## Time

- One tick is 1/60 s (`Mission::kTickSeconds`). `Highway` adds each frame's elapsed time and runs the whole ticks it covers, at most `kMaxTicksPerFrame`; a longer stall slows the game instead of making it jump. Views draw the latest tick (interpolation only if judder shows).
- Rates are per second. The original's are per frame, at a frame rate nobody has measured (its loop waits for a vblank after the drawing, so it is 60 divided by the vblanks a frame takes; *unverified*, to be measured in an emulator). Until then, original values convert at 20 frames a second, *provisional*: the player's top speed of 400 units a frame becomes 8000 units/s, 62 m/s at the scale below.
- Keys are applied at tick boundaries and the random generator is seeded once per mission, so the same seed and the same keystrokes at the same ticks replay a mission exactly on one build.
- Built: the fixed ticks in `Highway`.

## Space

- Positions are floats in the original's world units over the whole map (`WorldPoint`), +x east, +y north. A cell is 0x4000 units; `GetCell` floors. The original holds a cell plus a 16-bit position inside it, with carries (`0:1b56`); a float still resolves 1/16 unit at the map's far edge.
- Scale, assumed and used only to state tuning values: 128 units a meter (road 8 m wide, cell 128 m, eye height 100 units = 0.8 m).
- Angles are float radians counter-clockwise from east, as the original's 720 half degrees a turn; data angles convert where they are read (`FromHalfDegrees`). Trigonometry uses `<cmath>`, not the original's tables.

## Road

`Road` answers every question about the ground, for the mission and the views alike:

- **Cell types and exits** (`GetCellType`, `GetExits`): CARTE.BIN and the open sides of each type (`0:5eea`; type 0 has none, the original lists all four).
- **On road** (`IsOnRoad`): the point inside its cell's outline, the road shape filled even-odd. The 13 shapes (`roadCellShapes`, `0:7812`) come from the executable: they are the road's look. The station shapes join their driveway loops with one-unit slits, which even-odd filling closes. They are in `EngineAssets` (`road_shapes`), read by the host loader and packed for flash.
- **Lanes** (`GetLane`, `GetDriveway`): the path a computer driver follows through a cell, from the side it enters by to the side it leaves by, on the right-hand half of the road, generated as pieces that are straight or circular arcs: a line across, one arc about the cell's corner in a curve cell, and a line, a tight arc and a line for a turn at a junction, so it stays on the paved cross. Drivers chase a point ahead on the lane, which smooths the joins; clothoids can replace the arcs inside `Lane` if turns look mechanical. The original uses 24 fixed polylines (`0:9b40`, picked through `0:5f18`) and holds its cars 0..256 units right of the center line. The driveway lane of a station cell follows the driveway of the shape.
- **Stations**: the 20 cells of types 11 and 12, found on the map (the original lists the same cells at `1:23e0`), and the area in which a car counts as stopped at one (`0:340a`).
- **Scenery** (`GetScenery`): COOR_OBJ.BIN, the same objects in every cell of a type.

Built: cell types, exits, the on-road test, stations and scenery (`road.cpp`); the lanes are not.

## Cars

One `Vehicle` record for all cars: position, travel heading, slip (the body's angle to the travel direction), signed speed, body lift (bounce, bumps, shaking; the player's camera rides it), its controls and the impact under way. The original's record (`vehicles.md`, section 5) has 0x12c bytes, half of them unused, and reverses by turning the heading round and flipping the controls (section 7.3); here reverse is negative speed.

`Controls` are what any driver asks for: the wheel's position (-1 full right to 1 full left) and a throttle (-1 brake or reverse to 1 accelerate). `Drive` takes them as they are and moves the car, the same model for every car. Smoothing belongs to the input: held keys ramp the wheel in `Highway` (the original ramps it inside `DriveVehicle` for every car, 6 steps of 60 a frame, centering with speed), a steering wheel or a stick sets it directly, the AI smooths its own.

`CarCondition` holds what only the player's car tracks: fuel, engine temperature, damage, tires, the time spent off the road, gear and rpm (dashboard and engine note).

### Handling (reworked)

Replaces `DriveVehicle` (`0:3a2e`, `0:3d0c`); constants are tuned, not copied.

- Yaw from a bicycle model, speed x wheel / turning radius, capped by a grip limit; past it the car slides: the excess becomes slip, which decays and scrubs speed. The original: yaw = speed x wheel / (2 speed + 512) a frame, a slip state machine above a fixed speed x yaw.
- Rolling drag and engine braking: the original holds its speed without throttle.
- Down brakes to a stop and, held on, reverses; Up does the opposite. The original toggles reverse once per stop.
- Off the road: drag, a harder bounce, and a tire lost after too long at speed (original: 600 frames' worth of counter at speed 100 or more).
- Transmission (thresholds from `0:3bec`), rpm, fuel use (with speed squared) and temperature (with rpm) feed the dashboard, the engine note and the endings.

Built: throttle, brakes, reverse, drag, off-road drag and turning with the grip limit (`vehicle.cpp`); slip, bounce and the car's condition are not.

### Contacts

Each tick every car is tested against the scenery of its cell and the eight around it and against the other cars, footprint against object. The response is one `Impact` at a time instead of the original's five channels (`0:4068`):

| Touch | Original | Port |
|---|---|---|
| stones | bump profile, steering frozen, speed - 5 % a frame | `kBump`: the body rides a profile scaled by speed |
| bush | 2 frames of damage, steering x1.5 | drag and damage, no impact state |
| cactus, sign | push back, turned 90 degrees away from the contact, spinning until slow | `kSpin` |
| car | the same, both cars, red flash, penalty | `kSpin` on both |
| too long off road | shaking to a stop, a tire lost, PAGE_F1 | `kCrash` |

The original senses scenery for the player in its own cell only, in a box that follows the view; the criminal in its own cell; traffic never. Here every car touches everything.

## Computer drivers

- **Route**: a distance field over the road cells to the goal (`RouteField`), rebuilt when the goal changes; at each cell the car takes the open side whose neighbor is nearer. The original chooses greedily from the signs of the cell differences (`0:5a44`), which is not guaranteed to reach the goal (whether it fails on this map is unverified).
- **Following**: steer toward a point ahead on the lane, with a speed target from the cruise speed, the curve and the goal (the original: a lateral band and a heading error, `0:5c56`, `0:5e92`).
- **The criminal** heads for the nearest station not yet robbed, stops in its driveway, robs it unless the player is within one cell, then heads for the next; in shooting missions it fires at the windshield (`game.md`, "The criminal"; `vehicles.md`, 10.3).
- **Traffic**: spawned in a neighboring open cell ahead of the player, heading past the player (`0:540e`), removed out of range. The original has one traffic car; the port has room for `Mission::kMaxTraffic`, how many is a tuning choice.
- **Policies** (`AiPolicies`): the decisions a mission can swap, each a concept held in a `std::variant` so `Mission` stays a value: the criminal's next station (`NearestByRoad` by default; `NearestByManhattan`, the original; `AwayFromPlayer`), its reactions to the player (`LeaveWhenWatched`, the original), and where traffic appears and heads (`AheadOfPlayer`, the original). They read a `PolicyContext` and draw random numbers only from the mission's generator; `Highway` picks them.

## Mission rules

Kept as in the original (`game.md`, "Missions"; `vehicles.md`, section 10): arrest by mission type, the bounty that drains with time and penalties (a plain integer, not BCD), the end reasons in the original's priority, the criminal's shots (up to 10 holes; a hole in front of the driver kills).

Shooting no longer tests the sprite last drawn: the sight is a direction from the car (fixed ahead, jittered with speed, bobbing with the body), and a shot hits when that ray meets the target car's footprint within range. Windshield holes are stored as directions too; the views place them.

## The component

`Highway` follows presses and releases into held keys (left, right, accelerate, brake, fire) and one-shot toggles (S siren, T aim), as the other components do. Its phases: driving, paused (P), the spin-out picture (PAGE_F1 until a key, then on), and coasting: after the mission ends the cars roll to a stop before the ending (`0:6008`).

- **Station**: the mission reports the stop; `Highway` writes fuel, tires and whether the station is robbed into `GameState` and hands over to `Station`. Back in `OnEnter`, a mission under way resumes with the car at the driveway's exit (`LeaveStation`); otherwise a new one starts with the poster chosen in the office.
- **Ending**: the reason and the score go into `GameState`, then `MissionEnd`.
- **Map**: M pauses the game and shows the road map from above with the player's car and the criminal's; M again returns to the road. Blinking markers can come later. Built (`map_view.cpp`). Until the driver's view exists, `Highway` shows it live while driving.
- **Dashboard clock**: the digital clock right of the gauges has 13:24 painted into DES_TABB image 4. The port clears it and draws a running time in matching red segments: the mission time (`Mission::GetTick()` times `kTickSeconds`, minutes and seconds), *provisional*.

Built: the held keys, the fixed ticks, pause (P), the map (M) and abandoning (Escape); the station stop, the endings from play and the spin-out picture are not.

## Storage

Fixed arrays only. `Mission` is a value: copying it snapshots a mission (tests, replays). The largest parts are the route fields, 8 KB each: one per computer driver and two the policies share, 56 KB in all.

## Open

- The original's frame rate, which sets the time scale.
- How far the handling departs from the original; targets for its feel.
- How many traffic cars.
- Whether the criminal's robbery takes time (the original: instant once stopped).
- What the dashboard clock shows: mission time, or a time of day.
