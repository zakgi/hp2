# Game structure sizes and engine state

Baseline measured on 2026-10-20 before the controller refactor. The original ownership caused the
RP2350 to store two sets of components, and moving `Highway` left its views bound to the source
object's `Road`. The refactor below fixes both with one stable application state. Seven route
fields originally accounted for 57,372 bytes, or 96.9% of `Mission`. They now live in the active
highway state, where their storage overlaps other scenes; `Mission` retains the goals and plans.

The [complete baseline size inventory](state-sizes.csv) covers all 157 named struct and class
declarations under `src/` before the refactor, represented by 180 concrete types and template
instantiations: 112 core records, 29 host records and 39 target records. It includes private nested
types and implementation helpers. Each row gives its baseline declaration location, size and
alignment on each applicable build. Anonymous namespace
types retain their namespace label; other type names omit the common `hp2::` prefix.

## Shared ownership and active scene variants

[`EngineState`](../src/core/engine_state.hpp) owns the career, mission, driving and map views, and
the active scene's data in standalone records such as `OfficeState` and `TitleState`.
Controllers contain behavior; the scene records are namespace-level types. The root borrows the
platform's assets, screen, key queue and audio engine. It is constructed after assets are loaded
and cannot be copied or moved; RP2350 constructs it in static storage. The mission's road and the
views bound to it therefore stay at fixed addresses.

`Engine` stores only the active controller in `std::variant`, with `std::monostate` representing
an idle or stopped engine. `ComponentLike` requires default construction and the lifecycle hooks;
controllers may have private fields. Entry constructs a fresh controller, exit destroys it, and
switching to the same type also reconstructs it. All five current controllers remain empty.
Lifecycle methods receive the root by reference.

The root's `SceneState` is another variant: title, office, driving, station and ending data reuse
one buffer. Each scene constructs its alternative on entry and destroys it on exit. The mission,
views, selected AI policies, career seed stream and mission-under-way flag remain outside this
variant so a station visit preserves them. The scene records contain only data whose lifetime
ends with that scene, such as action stacks, cursors, held keys, pending frame time and the
highway's pathfinding workspace.

| Type after refactor | Host bytes | RP2350 bytes |
| --- | ---: | ---: |
| Each of `Title`, `Office`, `Highway`, `Station`, `MissionEnd` | 1 | 1 |
| `Engine<Title, Office, Highway, Station, MissionEnd>` | 16 | 8 |
| `EngineState` | 66,264 | 66,096 |
| Career `GameState` | 32 | 20 |
| `Mission` | 1,920 | 1,848 |
| `AiDriver` | 196 | 196 |
| `SceneState` variant, included in `EngineState` | 57,424 | 57,412 |
| `TitleState` | 3,280 | 3,208 |
| `OfficeState` | 64 | 40 |
| `HighwayState` | 57,408 | 57,408 |
| `MissionPathfinding`, included in `HighwayState` | 57,386 | 57,386 |
| Each `RouteField`, seven in the workspace | 8,198 | 8,198 |
| `StationState` | 4 | 4 |
| `MissionEndState` | 1,688 | 1,632 |

The state alternatives are included in `SceneState`, which is included in `EngineState`; these
sizes must not be added together. Before introducing variants, the shared root occupied 71,248
bytes on the host and 70,944 bytes on RP2350. The first scene variant reduced those sizes to
69,488 and 69,248 bytes. Moving pathfinding into that variant saves another 3,224 and 3,152 bytes
respectively. Replacing the empty controller tuple alone does not change `sizeof(Engine)`.

On RP2350, the root, engine, screen, audio engine, asset views and key queue now total 167,752
bytes, down from the baseline's 243,668 bytes: a saving of 75,916 bytes (74.1 KiB). This accounts
for the same game objects as the baseline, excluding linker placement padding, initialization
guards, platform storage, code in RAM, heap and stacks. The rebuilt firmware's linker RAM usage
is 329,364 bytes, down from 332,004 bytes before extracting pathfinding; this broader figure also
includes generated code and placement effects.

`HighwayState::pathfinding` owns two policy distance fields, the target's route and four traffic
routes. `Mission::Start` and `Step` take this workspace explicitly; headless simulations provide
their own. `AiDriver` keeps its goal, lanes, plan and timers in the copyable mission. Cached fields
track whether they have been built and which goal they describe. Starting a mission invalidates
the workspace, and using a field rebuilds it only when invalid or its goal changes. Cache validity
adds two bytes per field to the baseline size. A station visit destroys the workspace; returning
rebuilds distances as needed, with no policy decisions, RNG draws or lane replanning solely for
restoring caches. Traffic slot compaction copies only the small driver state, and its cache is
validated against the replacement driver's goal on next use.

Station operations now modify `Mission::GetCondition()` directly, and the station's robbed status
comes from the mission's station index and robbed bitset. Direct station startup presents station
zero. The ending reason lives only in mission progress, including abandonment by Escape. Career
score is still committed from bounty when a mission completes. A new mission resets the driving
view's position, stripe phase, aim and previous tick; a station visit preserves the mission.

`Mission` remains the headless model, with route caches excluded from snapshots. Rendering
storage stays in the root-owned view objects. Compressing route fields or sharing additional
rendering storage remains a separate step.

Both the host executable and RP2350 firmware build successfully. The host suite passes 203 tests;
the asset-image comparison is skipped because its packed image is absent. Regression checks cover
independent engine roots, moving the lightweight engine, replacing a controller while paused,
station services across engine transitions, new-mission view history, destruction and re-entry of
controllers with private fields, seed preservation across a station visit, and a copied mission
resumed with cold caches that are repeatedly invalidated over ten minutes of simulation. Four
seeds run for 36,000 ticks each produce the same per-tick event, vehicle motion and progress
hashes as before cache extraction. The unoptimized
AddressSanitizer probe checks a moved engine and transitions through every scene without diagnostics.
The RP2350 compiler reports 72 bytes for `Highway::OnEnter`'s own stack frame; the large variant
alternative is constructed in place.

## Baseline measurement scope

All sizes below are bytes from compiler record layouts, including inline arrays and padding.
They exclude memory reached through spans, references, pointers and dynamic containers. Parent
sizes already include their value members: `Engine`, `Highway`, `Mission` and `AiDriver` must not
be added together as independent allocations.

| Build | Compiler and library | Configuration |
| --- | --- | --- |
| Host | Apple Clang 17.0.0, libc++, arm64 macOS | Existing host compile database, C++23 Debug |
| RP2350 | Arm GNU 15.3.1, libstdc++, Cortex-M33 | Existing board compile database, C++23 Release, softfp |

The survey compiled all 59 application translation units with their existing flags and two
supplemental header probes for the flash layout and key map. All 61 syntax checks passed. Host
layouts came from `-Xclang -fdump-record-layouts-complete`; target layouts came from
`-fdump-lang-class`. The current board entry point was also compiled to a scratch object, and
`arm-none-eabi-nm -S -C` confirmed that both sets of component storage are emitted. Existing ELF
sizes were not used because that build predates the current headers.

Enums, concepts, aliases, lambdas, standard library internals, SDK types and third party types
are outside the inventory; their storage is included when embedded in an application type.
Template definitions are represented by their concrete application instantiations. The key map
and flash image have supplemental measurements on both ABIs even where only one application
build includes them. Empty policy and board types have size 1; some need no standalone storage.

## Baseline large objects

| Type | Host | RP2350 | What it owns or describes |
| --- | ---: | ---: | --- |
| `Engine<Title, Office, Highway, Station, MissionEnd>` | 71,360 | 71,000 | All five components by value, plus active component |
| `Screen` | 67,096 | 67,084 | 64,000 pixels, four 768-byte palettes, selection metadata |
| `Highway` | 66,184 | 66,040 | Mission, driving view, map view, input and timing state |
| `Mission` | 59,272 | 59,200 | Road, cars, AI drivers, route caches, rules and random state |
| `AudioEngine` | 33,472 | 33,344 | Ring, four voices, music player and clock |
| `AudioEngine::Ring` | 32,896 | 32,896 | 8,192 stereo frames plus cursor alignment |
| `AiDriver` | 8,388 | 8,388 | One route field, two lanes, plan and speed |
| `RouteField` | 8,196 | 8,196 | 4,096 16-bit distances plus goal cell |
| `DriverView` | 6,800 | 6,768 | Road rows, visible objects and view history |
| `RoadRows` | 4,720 | 4,720 | 62 rows, each reserving eight road spans |
| `Title` | 3,312 | 3,224 | Dependencies, four action slots and stage |
| `RoadsideObjects` | 2,016 | 2,008 | 160 visible objects, five visible cars and counts |
| `EngineAssets` | 1,912 | 1,080 | Borrowed asset views and small range tables |
| `MissionEnd` | 1,720 | 1,648 | Dependencies, two action slots, ending and stage |
| `KeyMap` | 1,272 | 1,272 | Fixed forward and reverse mapping tables |

The declared `GameState` is only 48 bytes on the host and 32 bytes on RP2350. Most mutable game
state lives elsewhere. `Office` is 96/56 bytes and `Station` is 40/20 bytes, so small scene objects
and reference slots are not the main memory cost.

## Baseline mission storage

| Contents of `Mission` | RP2350 bytes | Count and ownership |
| --- | ---: | --- |
| Route fields | 57,372 | Two policy distance fields plus one in each of five AI drivers |
| Rest of the five AI drivers | 960 | 192 bytes each, excluding their route field |
| Vehicles | 360 | Player, target and four traffic slots, 60 bytes each |
| Road | 316 | Asset views, range tables and station list |
| Mission progress | 124 | Rules, bounty, robbed stations, sight, holes and ending |
| Car condition | 28 | Fuel, temperature, damage, tires, gear and rpm |
| Remaining members and padding | 40 | Random state, policies, counts, station, flags and tick |
| Total | 59,200 | |

Traffic capacity is reserved even when `traffic_count_` is zero. The seven route fields have
different goals and purposes, so they cannot simply alias one shared array. Their distance arrays
are derived data, while an AI driver's chosen goal is part of simulation state. Separating those
would make snapshots smaller and allow a later cache or map representation change without
changing the rules.

The actual vehicles and car condition occupy only 388 bytes. Packing their flags or flattening
`Vehicle` is a much smaller opportunity than eliminating the second component set or changing
route storage.

## Rendering and presentation storage

`DriverView` contains `RoadRows` (4,720 bytes), `RoadsideObjects` (2,008 bytes on RP2350), and 40
bytes of references and history. These are mostly drawing workspaces, not mission state. The
projection object itself is just two references: 16 bytes on the host and 8 bytes on RP2350.

| Rendering record | Host | RP2350 | Use |
| --- | ---: | ---: | --- |
| `RoadRow` | 76 | 76 | Depth, scale, eight spans, count and padding |
| `RoadSpan` | 8 | 8 | Two float edges |
| `RoadsideObjects::ViewObject` | 12 | 12 | Projected scenery entry |
| `RoadsideObjects::ViewCar` | 16 | 16 | Projected car entry |
| `ViewDescription` | 24 | 24 | Projection constants |
| `ViewInput` | 16 | 16 | Eye position, heading and height |
| `DashboardInput` | 32 | 32 | Temporary dashboard input |
| `RoofText` | 24 | 24 | Temporary roof text input |
| `ScreenPalette` | 768 | 768 | 256 RGB colors |
| `Fade` | 792 | 788 | Palette reference, a full palette copy and timing |
| Title action stack | 3,272 | 3,204 | Four slots sized for the largest action |
| Ending action stack | 1,640 | 1,604 | Two slots sized for a fade |

The cockpit inputs are small, temporary projections of authoritative state. Keeping those
records is useful; they do not cause the long-lived duplication. Before the refactor,
`Mission::Start` reset its tick to zero while `Highway::OnEnter` retained the view's
`last_position_`, `travel_`, `aim_` and `last_tick_`. The unsigned subtraction in
`DriverView::Draw` could wrap on the next mission. The first pass adds an explicit view reset.

Fade storage is worth revisiting after ownership is fixed. Every action slot reserves a full
palette snapshot even when holding a much smaller `Hold` action. Reducing or sharing this storage
requires checking which palettes and actions must coexist; it is not part of the 70,988-byte
component saving below.

## Baseline state ownership defects

### Component moves retain references into the source object

Before the refactor, [`Highway` construction](../src/core/highway.cpp) bound `DriverView`, its
`RoadProjection`, and `MapView` to `mission_.GetRoad()`. [`Engine`](../src/core/component.hpp) then
moved each component into an owning tuple. The implicit moves copied those reference bindings;
they did not rebind them to the destination's `mission_.road_`.

In [`src/main.cpp`](../src/main.cpp), the source `Highway` was a temporary destroyed at the end of
the engine declaration, leaving dangling road references. In the
[`board entry point`](../src/boards/adafruit_feather_rp2350/main.cpp), the source was static and
survived, so the views remained tied to the extra source object instead of the engine's mission.
This is a correctness reason to establish stable ownership before changing layouts.

A scratch reproduction constructed `Engine<Highway>{Highway{...}}` over a valid-size, zero-filled
road map, then called `Start(kHighway)`. Compiling the probe and core sources with `-O0 -g
-fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer` reported
`stack-use-after-scope` in `RoadProjection::Project -> Road::GetOutline -> Road::GetCellType`.
The accessed memory was inside the destroyed `Highway` temporary. The same probe at `-O1` did
not diagnose the invalid access; the unoptimized sanitizer result confirms the lifetime defect.

### The board reserves two complete component sets

The board constructed static `title`, `office`, `highway`, `station` and `mission_end`, then moved
them into a second static `Engine` that stored them by value. Moving inline arrays left their
source storage allocated.

| Application objects in the board entry point | Bytes |
| --- | ---: |
| Engine with its component tuple | 71,000 |
| Original five component objects | 70,988 |
| Screen | 67,084 |
| Audio engine | 33,344 |
| Engine asset views | 1,080 |
| Key queue | 140 |
| Current game state | 32 |
| Sum of these object sizes | 243,668 |

One component set would reduce that sum to 172,680 bytes, saving 70,988 bytes (69.3 KiB).
These sums exclude linker placement padding, initialization guards, platform storage, code in
RAM, heap and stacks. They are an accounting of the listed objects, not a complete firmware RAM
budget. The core subtotal for one set on the host, using the same list, is 174,040 bytes.

The refactor should move persistent storage out of the components and construct it once in the
shared state. Keeping the current large components at stable addresses would address their
movement, but would leave ownership in the controllers. Changing the tuple to references alone
would also require removing the host's temporary component arguments to avoid dangling bindings.

### Shared state is copied at transitions

| Data | Baseline authorities and transfer | Shared authority |
| --- | --- | --- |
| Fuel and tires | `Mission::condition_` copied to `GameState` on station entry, edited there, copied back by `LeaveStation` | `Mission::GetCondition()`, edited directly by station operations |
| Robbed station | `MissionProgress::robbed` plus a copied `GameState::station_robbed` flag | Robbed bitset indexed by the current station |
| End reason | `MissionProgress::end_reason` copied to `GameState::end_reason`; Escape writes only the latter | One ending value in mission state |
| Score and bounty | Career score seeds mission bounty; the resulting bounty is committed on completion | Keep their distinct meanings and make the commit explicit |
| Mission lifecycle | `GameState::mission`, `Highway::mission_under_way_`, highway phase and progress ending | Explicit lifecycle transitions in the shared state |
| AI policies | Selected in `Highway`, copied into `Mission` | One selected configuration for the active mission |

Direct station and ending startup options will need to initialize the same mission fields that
normal transitions use. Keeping parallel preview fields would reintroduce the current problem.

## Further state decomposition

The stable root, lifecycle interface and separation of pathfinding from mission snapshots are
implemented. The following remains a possible decomposition for rendering workspaces and road
ownership; it is not the current root's member layout.

| Root member | Contents and lifetime |
| --- | --- |
| `assets` | Const reference to `EngineAssets`; host asset manager or flash binding outlives the engine |
| `road` | One canonical `Road` over those assets, read-only to components |
| `career` | Score, available missions and selected poster |
| `mission` | Cars, car condition, rules, AI plans and goals, random state, current station and tick |
| `scene.highway.pathfinding` | Rebuildable route distance arrays and cache validity, while driving |
| `seeds`, `policies`, mission lifecycle | Configuration and random stream that survive scene transitions |
| `scene` | Variant of title, office, highway, station and ending presentation state and actions |
| `render` | Road rows, visible objects and driving-view history |
| `screen` | Framebuffer and palettes |
| `keys` | One event queue shared with the platform input backend |
| `audio` | Mixer, voices, music state and output ring |

Components operate on this root instead of owning large graphs of mutually bound
objects. Keep controllers lightweight and default-constructible: dependencies arrive through the
lifecycle arguments, and phases, cursors, action stacks, input tracking and animation history
belong to the corresponding standalone scene records. Private controller fields are supported
when their lifetime should end on a transition. The implemented interface is:

```cpp
void OnEnter(EngineState& state);
ComponentType Step(EngineState& state, float delta_seconds);
void OnExit(EngineState& state);
```

`Engine` selects and dispatches these lightweight controllers through its active variant.
Persistent mission data and drawing buffers remain in the root. Moving a controller must not
move any persistent game storage or invalidate references into it.

Pass the needed nested state to lower-level simulation and drawing functions. For example,
mission stepping uses mission data, AI runtime, the road and commands; it still runs without a
screen or audio device. Drawing reads mission data and updates render history and output buffers.
This keeps one application entry point while preserving the existing headless simulation boundary.

Snapshot only simulation state. Framebuffers, audio rings, asset references and projection
workspaces should not become part of mission snapshots. Preserve AI route goals in the snapshot
and rebuild distance arrays without consuming random numbers. The root's stable lifetime also
protects the music player's span into the audio engine's voices and any active fade's palette
reference.

Centralizing ownership does not by itself save the route or rendering buffer sizes. The immediate
memory saving comes from constructing each object once. Route compression and sharing drawing
scratch are subsequent changes with separate capacity and behavior checks.

## Refactor order

1. Establish the shared root and its owned data types. Construct storage once at its final address
   in both entry points, with one canonical road and no component-owned copies.
2. Change the component contract to take the shared state in its lifecycle methods. Migrate
   controllers to empty, default-constructible types, moving each field to the root as its owner
   changes. Remove the board's original component storage and the engine's large component
   arguments. Retain keyboard press and release events as the input model, and verify that views
   use the canonical road after engine construction.
3. Move mission data into the root and make station and ending components use it directly. Test
   service changes, station return, normal mission completion, Escape and direct scene startup.
4. Separate render history and scratch from simulation; reset history for a new mission and
   preserve the active mission across a station visit. Check mission restart and map rendering.
5. Separate AI goals and plans from route caches, preserve deterministic replay, then evaluate
   route storage. Re-measure before considering smaller layout changes.

The refactor implements shared ownership, active controller and scene variants, direct servicing
and ending state, the view history reset, and pathfinding caches owned by the driving scene.
The measured root size is recorded above. Route compression and further separation of rendering
workspaces can proceed without putting persistent data back in controllers.

## Host and asset storage

| Type | Host bytes | Additional storage |
| --- | ---: | --- |
| `host::Renderer` | 256,064 | Includes 256,000 RGBA staging bytes; SFML and GPU allocations are external |
| `host::AssetManager` | 9,296 | Owns decoded data in dynamic containers; their allocations are external to `sizeof` |
| `host::AudioOutput` | 1,072 | Includes a 1,024-byte output chunk; SFML stream resources are external |
| `host::SfmlInput` | 120 | Input backend bookkeeping |
| `flash::asset_layout::AssetImage` | 1,545,000 | Same layout on both ABIs; resides in target flash |

The packed image contains pictures (640,000), banks (534,300), title animation (224,504), title
music (69,676), sounds (43,600), fonts (12,032), palettes (4,416), map (4,096), scenery (11,150),
road shape points (1,224) and two bytes of final alignment padding. The small `EngineAssets`
descriptor borrows this data; placing its payload inside mutable engine state would defeat the
current flash design. The complete CSV lists every concrete bank layout and all remaining small
core, loader and platform records.
