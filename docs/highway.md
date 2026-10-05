# Highway: the mission and its views

Status: being implemented; the headers declare the planned interfaces, and what is built so far is noted in each section. The original's behavior is the reference (`vehicles.md`, `game.md`, `renderer.md`); this file describes how the port is built and where it departs from it on purpose. Choices marked *provisional* wait for a decision.

## Parts

| Part | Owns | Knows nothing of |
|---|---|---|
| `Mission` (`src/core/mission.hpp`) | one mission: the cars, the AI drivers, the rules, the random numbers; advanced one fixed tick at a time | screen, keys, sound |
| `Road` (`road.hpp`), `world.hpp` | units, cells, angles; queries on the road map, the road shapes, the lanes and the scenery | cars |
| Cars (`vehicle.hpp`), AI (`ai.hpp`) | a car's state and how it moves; how a computer driver picks its way and follows its lane | rules |
| `Highway` component (`highway.hpp`) | keys to commands, the tick clock, events to sounds and scenes, the view shown | rules |
| Views: `DriverView` (`driver_view.hpp`), `MapView` (`map_view.hpp`) | drawing a mission: the driver's view and the map | rules |

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
- **On road** (`IsOnRoad`): the point inside its cell's outline, the road shape filled even-odd. The 13 shapes (`roadCellShapes`, `0:7812`) come from the executable: they are the road's look. In the two station shapes the far end of the driveway loop lies one unit off the road's edge (a gap in type 11, an overlap in type 12), so a strip one unit wide and 1280 long between them counts as off the road. They are in `EngineAssets` (`road_shapes`), read by the host loader and packed for flash.
- **Lanes** (`GetLane`, `GetDriveway`): the path a computer driver follows through a cell, from the side it enters by to the side it leaves by, on the right-hand half of the road, generated as pieces that are straight or circular arcs: a line across, one arc about the cell's corner in a curve cell, and a line, a tight arc and a line for a turn at a junction, so it stays on the paved cross. Drivers chase a point ahead on the lane, which smooths the joins; clothoids can replace the arcs inside `Lane` if turns look mechanical. The original uses 24 fixed polylines (`0:9b40`, picked through `0:5f18`) and holds its cars 0..256 units right of the center line. A lane runs 256 units right of the center line; a turn at a junction has a radius of 768 units to the right and 1536 to the left. The driveway lane of a station cell follows the driveway of the shape: off the road by one ramp, along the pad past the pumps, which are halfway, and back by the other ramp, leaving the cell by the side opposite the one it entered by.
- **Stations**: the 20 cells of types 11 and 12, found on the map (the original lists the same cells at `1:23e0`), and the area in which a car counts as stopped at one (`0:340a`).
- **Scenery** (`GetScenery`): COOR_OBJ.BIN, the same objects in every cell of a type.

Built: all of it (`road.cpp`).

## Cars

One `Vehicle` record for all cars: position, travel heading, slip (the body's angle to the travel direction), signed speed, body lift (bounce, bumps, shaking; the player's camera rides it), its controls and the impact under way. The original's record (`vehicles.md`, section 5) has 0x12c bytes, half of them unused, and reverses by turning the heading round and flipping the controls (section 7.3); here reverse is negative speed.

`Controls` are what any driver asks for: the wheel's position (-1 full right to 1 full left) and a throttle (-1 brake or reverse to 1 accelerate). `Drive` takes them as they are and moves the car, the same model for every car. Smoothing belongs to the input: held keys ramp the wheel in `Highway` (the original ramps it inside `DriveVehicle` for every car, 6 steps of 60 a frame, centering with speed), a steering wheel or a stick sets it directly, the AI smooths its own.

`CarCondition` holds what only the player's car tracks: fuel, engine temperature, damage, tires, the time spent off the road, gear and rpm (dashboard and engine note).

### Handling (reworked)

Replaces `DriveVehicle` (`0:3a2e`, `0:3d0c`); constants are tuned, not copied.

- Yaw from a bicycle model, speed x wheel / turning radius, capped by a grip limit; past it the car slides: the body goes on turning ahead of the way the car goes, and the angle between them is the slip, which decays, turns the car part of the way after its body and scrubs speed. The original: yaw = speed x wheel / (2 speed + 512) a frame, a slip state machine above a fixed speed x yaw.
- The body bounces on its springs as the car moves, faster with speed and four times as hard off the road (the original's wave, `0:4930`); the player's eye rides it.
- Rolling drag and engine braking: the original holds its speed without throttle.
- Down brakes to a stop and, held on, reverses; Up does the opposite. The original toggles reverse once per stop.
- Off the road: drag, a harder bounce, and a tire lost after too long at speed (original: 600 frames' worth of counter at speed 100 or more).
- Transmission (thresholds from `0:3bec`), rpm, fuel use (with speed squared) and temperature (with rpm) feed the dashboard, the engine note and the endings.

Built: throttle, brakes, reverse, drag, off-road drag, turning with the grip limit, slip and bounce, and the player's gear, engine speed, fuel and temperature by the original's rules (`vehicle.cpp`, `CarCondition::Update`); the tire lost after 15 s off the road at 1500 units a second or more, with the car shaking to a stop (`mission.cpp`). The original's threshold, 2000 units a second, is out of reach here: the desert's drag holds a car at full throttle just under it.

### Contacts

Each tick every car is tested against the scenery of its cell and the eight around it and against the other cars, hit box against object. The response is one `Impact` at a time instead of the original's five channels (`0:4068`):

| Touch | Original | Port |
|---|---|---|
| stones | bump profile, steering frozen, speed - 5 % a frame | `kBump`: the body rides a profile scaled by speed |
| bush | 2 frames of damage, steering x1.5 | drag and damage, no impact state |
| cactus, sign | push back, turned 90 degrees away from the contact, spinning until slow | `kSpin` |
| car | the same, both cars, red flash, penalty | `kSpin` on both |
| too long off road | shaking to a stop, a tire lost, PAGE_F1 | `kCrash` |

The original senses scenery for the player in its own cell only, in a box that follows the view; the criminal in its own cell; traffic never. Here every car touches everything.

- A car's hit box is 240 units wide and 500 long on the ground, about its middle and turned with its body (the original's box: 120 units left to 220 right, 200 behind to 400 and a frame's travel ahead). It runs into the scenery under the half that leads the way it moves, at 100 units a second or more, and into another car whose middle comes within a car's width and length of its own.
- A contact between cars counts once, when it starts: the slower car is knocked on at half the speed of the faster, both spin, and with the player's car in it the bounty loses 20 dollars a car, and 20 more when the other car is the criminal's, unless the mission is a roadblock. In a roadblock mission the criminal running into the player's standing car is the arrest. Nothing keeps two cars from passing through each other after the contact, as in the original.
- Damage, of a budget of 10000: stones the car's speed a frame, a cactus or a sign twice that, a car four times, a bush the speed for every frame in it, a lost tire's shaking the same.

Built: all of it (`vehicle.cpp` for the bump and the spin, `mission.cpp` for the rest). The roadblock arrest has no test that plays it.

## Computer drivers

- **Route**: a distance field over the road cells to the goal (`RouteField`), rebuilt when the goal changes; at each cell the car takes the open side whose neighbor is nearer. The original chooses greedily from the signs of the cell differences (`0:5a44`), which is not guaranteed to reach the goal (whether it fails on this map is unverified).
- **Following**: steer toward a point ahead on the lane, with a speed target from the cruise speed, the curve and the goal (the original: a lateral band and a heading error, `0:5c56`, `0:5e92`).
- **The criminal** heads for the nearest station not yet robbed, stops in its driveway, robs it unless the player is within one cell, then heads for the next; in shooting missions it fires at the windshield (`game.md`, "The criminal"; `vehicles.md`, 10.3).
- **Traffic**: spawned in a neighboring open cell ahead of the player, heading past the player (`0:540e`), then on to a station picked at random (the original turns it round toward the player again), and removed more than two cells from the player. The original has one traffic car, and so has the port for now; it has room for `Mission::kMaxTraffic`, how many is a tuning choice.
- **Policies** (`AiPolicies`): the decisions a mission can swap, each a concept held in a `std::variant` so `Mission` stays a value: the criminal's next station (`NearestByRoad` by default; `NearestByManhattan`, the original; `AwayFromPlayer`), its reactions to the player (`LeaveWhenWatched`, the original), and where traffic appears and heads (`AheadOfPlayer`, the original). They read a `PolicyContext` and draw random numbers only from the mission's generator; `Highway` picks them.

Built: the route field, the lane following, the criminal's round of the stations and the traffic car (`ai.cpp`, `mission.cpp`); the criminal's shots are not.

## Mission rules

Kept as in the original (`game.md`, "Missions"; `vehicles.md`, section 10): arrest by mission type, the bounty that drains with time and penalties (a plain integer, not BCD), the end reasons in the original's priority, the criminal's shots (up to 10 holes; a hole in front of the driver kills).

Shooting no longer tests the sprite last drawn: the sight is a direction from the car (fixed ahead, jittered with speed, bobbing with the body), and a shot hits when that ray meets the target car's hit box within range. Windshield holes are stored as directions too; the views place them.

The pull-over arrest asks for the criminal's car in the player's cell, less than 1000 units ahead inside the driver's field of view (the original: its sprite on screen) and heading within a quarter turn of the player's way, with the siren on. In a station's area the player's car brakes by itself, as the original's, and its stop is the station stop. Once a mission is over both cars lose their throttle and slow to a stop.

The gun (T takes it out, Space fires) shoots once every 3 ticks, a frame of the original's, while the trigger is held. The sight's line starts 10 rows under the horizon, strays with speed and drops as the body rises; it hits the nearest car whose hit box it meets before the ground, which puts the gun's range at about 2500 units (the original: the car whose picture is under the sight, at one of its 6 largest sizes). Five hits arrest the criminal in a shooting mission. In the other missions a hit on the criminal's car takes 20 dollars; the original takes 20 every frame from the fifth hit on, which ends the mission. A hit on a traffic car takes 40 and spins it out.

The criminal fires every 18 ticks while its policy says so, in the original's shooting missions with the player in its cell, straight along its way toward the side the player is on. The bullet marks the windshield where its line crosses a plane 350 units ahead of the eye, inside the view; a hole within 20 pixels of the view's middle, or an eleventh hole, kills.

Built: the bounty's drain and penalties, the three arrests, every ending, the station stop and the way out of it, the player's shots and the criminal's (`mission.cpp`). No test plays the roadblock arrest or a hole that does not kill.

## The component

`Highway` follows presses and releases into held keys (left, right, accelerate, brake, fire) and one-shot toggles (S siren, T aim), as the other components do. Its phases: driving, paused (P), the spin-out picture (PAGE_F1 until a key, then on), and coasting: after the mission ends the cars roll to a stop before the ending (`0:6008`).

- **Station**: the mission reports the stop; `Highway` writes fuel, tires and whether the station is robbed into `GameState` and hands over to `Station`. Back in `OnEnter`, a mission under way resumes with the car at the driveway's exit (`LeaveStation`); otherwise a new one starts with the poster chosen in the office.
- **Ending**: the reason and the score go into `GameState`, then `MissionEnd`.
- **Map**: M pauses the game and shows the road map from above with the player's car and the criminal's; M again returns to the road. Blinking markers can come later. Built (`map_view.cpp`).
- **Dashboard clock**: the digital clock right of the gauges has 13:24 painted into DES_TABB image 4. The port clears it and draws a running time in matching red segments: the mission time (`Mission::GetTick()` times `kTickSeconds`, minutes and seconds), *provisional*.

Built: the held keys, the fixed ticks, pause (P), the map (M), the siren and gun switches (S, T), abandoning (Escape), the station stop and the return from it, the spin-out picture, and the endings from play after the cars have rolled to a stop, with what is left of the bounty as the score; a new mission's bounty counts on from the score.

- **Sounds**, on the original's four channels (`UpdateSoundEffects`, `0:1162`): the engine's sample loops on the first voice, its rate following the speed as the original's period does (640 less the speed in units a frame, the sample's own rate at 540); the siren loops on the second while it is on; a shot, the player's or one of the criminal's through the windshield, and a crash share the third, the crash first; the tires squeal on the fourth while the car slides at 2000 units a second or more with a slip of 0.15 rad (the original's 50 half degrees, 0.44 rad, is more than a slide reaches here), and once, at half volume, as the car leaves the road at that speed. The original restarts the engine's sample at every change of speed; here its rate changes while it plays. Paused, on the map and behind the spin-out picture the voices are silent.

## The driver's view

`DriverView` draws the mission from the player's car: the eye at the car's position, looking along its body, 100 units above the ground plus the body's lift. It splits the screen at row 132, the dashboard's, and draws the view above it, then the car around it. The original's pipeline is in `renderer.md`; the numbers of the projection (`kDriverView`: focal length 256, center column 160, horizon row 70) are its.

- **Colors** (`view_palette.hpp`): the original changes colors down the screen (`viewPalette`, `0:a520`, 39 sets). The upper viewport holds them side by side: the view's set at entry 0, the roof strip's at 16, the sky's 26 shades from 32, and the ground's 11 sets, hazier toward the horizon, from 64. A row is drawn with the entries of its set; the dashboard's set is the lower viewport's palette.
- **Sky and backdrop** (`DrawSkyHorizon`): each row of sky in its shade, then the six backdrop strips, a full turn of horizon, placed by the heading. The backdrop's own sky pixels take the shade of their row.
- **Road** (`RoadProjection`, `DrawRoadSurface`): each row of ground is a band of depth ahead of the eye. Lines across the view at depths inside the band, at most 512 units apart, are cut by the road outlines of the cells they cross, even-odd as `IsOnRoad`; what the lines of a row find, joined, is the row's road, up to 8 spans. The surface is sand, the spans in asphalt, and white dashed lines inside the spans' ends. Every cell out to 4 cells ahead counts, nothing is clipped, and a road that crosses the view far away is always met by a line, so it does not come and go between frames. The original projects and fills the outlines of the player's cell and one neighbor.
- **Roadside objects** (`RoadsideObjects`): the scenery of the cells in view out to one cell ahead (the original: 8500 units, in the one or two cells it draws), far to near, each picture at the size of its depth and with the entries of the ground's set where it stands, so the haze takes it too. Signs show their front, back or edge by the way they face; a station's sign shows the arrow toward the station.

- **Cars** (`RoadsideObjects`): the criminal's car and the traffic's stand among the roadside objects, far to near, out to one cell ahead. A car shows one of 24 sides by the way its body points against the line from the eye to it, each side a picture of VOITURE0-6 at the size of its depth, ten of them flipped left to right (`Screen::BlitMaskedMirrored`; the original flips the pictures in place). Its wheels stand at row 138 at the lowest, so a car close ahead shows over the hood. A car's picture uses colors 1 to 7, which the sky and the haze never change: white, black, gray and four shades of paint. The criminal's red is in the view's set, as the original's one paint; each traffic car in view has a copy of those eight colors with its own scheme at entries 240 and 248, so two schemes show at once. The eight paints (`0:a6f4`) are constants in `view_palette.hpp`. The original draws one car at a time, the criminal's before the traffic's.

- **The car** (`cockpit.hpp`): the hood's edge and the roof strip over the view, then the dashboard picture, the steering wheel, the needles and the two hands in the lower viewport. The hands ride a circle about the wheel's hub and turn with the wheel, 30 degrees either way at full lock, from `Controls::steer`. While the car moves the wheel and the hands shift a pixel up and back every 3 ticks, the original's vibration at its assumed 20 frames a second.
- **Needles**: the speedometer's turns with the size of the car's speed, 235.7 degrees at 8000 units a second (the original's 400 a frame); the tachometer's with `CarCondition::rpm` in the original's units; the two small gauges point at one of the original's 16 tips for the fuel and the temperature.

- **Roof text** (`DrawRoofText`; `DrawHudText`, `0:e1b4`), in the strip's boxes from left to right: the map cell the player's car is in, column then row, and the compass point it heads for; the bounty, five digits; the stations robbed; the compass point the criminal's car heads for and its cell. The player's pieces and the bounty are in LETTRE2.BIN, the rest in LETTRE1.BIN, in the roof's colors. The original has four more characters it never shows: the flag that draws them is never set.
- **The gun**: with the gun out the left hand leaves its place on the wheel, over three of the original's frames, for where full left lock would take it, and goes back as slowly when the gun is put away. While it is there the sight (BALLE.IMG's first image) stands where the mission's sight points, with the flash of a hit over it after a shot that hit. The holes of the criminal's bullets (the second image) stay on the windshield where they came through.

Not drawn yet: the gauges' warning lights, whose pictures are in the executable and not in the assets, and the red flash of a contact with another car and of the bullet that kills. The mission counts the flash (`MissionProgress::flash_seconds`); showing it waits for a palette the screen can swap in. The dashboard's clock still shows the 13:24 painted into its picture.

Open: the strip one unit wide at the far end of a station's driveway (see "Road") shows as a line of sand across the driveway's mouth on the rare frame where a line across the view falls inside it. Closing it where the shapes are decoded would fix the view and `IsOnRoad` together.

## Storage

Fixed arrays only. `Mission` is a value: copying it snapshots a mission (tests, replays). The largest parts are the route fields, 8 KB each: one per computer driver and two the policies share, 56 KB in all.

## Open

- The original's frame rate, which sets the time scale.
- How far the handling departs from the original; targets for its feel.
- How many traffic cars.
- Whether the criminal's robbery takes time (the original: instant once stopped).
- What the dashboard clock shows: mission time, or a time of day.
