# Game flow, missions and scenes

Evidence: Ghidra `hp2`; detailed per-function notes in `work/analysis/events-sound.md` and `work/analysis/frontend.md`. *Unverified* marks inferences not tested in an emulator.

## Program flow

1. **Title** (`main` `0:a908`): LOGO.CPV fades in/out; PRESENT.CPV plus the PRESENT.DIF animation with HIGHWAY.MUS playing; NAME.IMG overlays; "LICENCE" patched to "LICENSE" by copying a glyph (`0:ad92`). Common assets load (CARTE.BIN, fonts, scenery banks, sound effects, BALLE.IMG). Fire starts the game; the music stops for good. Stages and timing in "Opening presentation" below.
2. **Office** (`OfficeSelectMission`, `0:c3aa`), before every mission: point and click (mouse, or joystick moving the pointer) on three desk drawers holding six wanted posters. Each mission can be played once (`missionAvailable[6]`).
3. **Mission setup** (`0:b17e`-`0:b970`): target car start = one of 16 crossroads (`targetStartCells`, random from the beam position), player start = one of 3 positions (`playerStarts`), vehicle records initialised, game assets loaded (COOR_OBJ.BIN, DES_TABB, DEC_FOND, PAN_*, PST_*, VOITURE0-6), view palette and sound effects installed.
4. **Driving loop** (`0:ba7c`), once per frame, until `CheckMissionEnd` jumps back to the office.
5. **End screens** (`CheckMissionEnd`, `0:6008`), then back to step 2; after a failure, or after the sixth arrest, the career restarts (`fullRestart`: score and missions reset).

## Opening presentation

`main` runs it as straight-line code with busy waits. Durations are counted from the 68000 instructions at 7.15909 MHz (NTSC) with vertical blanks taken as 1/60 s; they are not measured, and the music interrupt's share of the processor is not counted (*unverified*). A fade step is `FadePaletteList` at one level (7..0 in, 0..7 out; `palette.hpp`, `FadedColor`), two `FlipScreens` and 65536 `dbf`: about 0.125 s, 1 s for a whole fade.

| Stage | Code | Duration |
|---|---|---|
| LOGO.CPV fades in under its header palette (made into a one-segment list in the file buffer) | `0:a95c`-`0:aa34` | 1 s |
| Logo stays while PRESENT.CPV, PRESENT.DIF and HIGHWAY.MUS load | `0:aa38`-`0:aaa4` | disk time |
| `ClearScreen(frontScreen)` cuts to black, then the logo palette fades out on the black screen | `0:aaf2`-`0:ab68` | 1 s |
| Music starts; PRESENT.CPV fades in under `titlePalette` (split at row 36) | `0:ab6c`-`0:ac3e` | 1 s |
| 16 x 65536 `dbf`, the picture converted to Atari ST layout (`0:ac52`), `PlayDIF`'s own conversion and copy, 7 x 61441 `dbf` | `0:ac42`-`0:10aa` | 2.15 s |
| 27 play list steps (`presentPlayList`): `ApplyDIFFrame` into the ST buffer (70 cycles + 56 a run + 46 a word), `STScreenToPlanar` straight into the displayed screen, 2 x (delay + 1) `dbf`. Frame 10 three times and frame 11 seven times make the lettering flash | `0:10ae`-`0:10f8` | 3.47 s |
| NAME.IMG images 2 at (10,189) and 3 at (258,143), masked; the 8x8 cell at (240,192) copied to (160,192) ("LICENSE"); one `FlipScreens` | `0:acd4`-`0:add4` | |
| Common assets load, `WaitFire`, `MusicStop`, `titlePalette` fades out | `0:adda`-`0:b05e` | disk time, 1 s |

The port's `Title` (`src/core/title.cpp`) plays these stages on an action stack with the same timing, a 2 s hold in place of the first disk wait and none for the second. Space or Enter skip to the finished title, and on it fade out and leave; Escape quits. HIGHWAY.MUS plays from the title's fade-in to its fade-out, as in the original.

## The port's screens between missions

The port's `Office`, `Station` and `MissionEnd` components (`src/core/`) share a `GameState`: the score, the posters left, the mission under way, how it ended, and the fuel, tyres and robbed flag the station reads.

- **Office** (`office.cpp`): as the original (section 3 of the front-end notes): the desk under the dim palette, then lit; a pointer (BUREAU.IMG image 10) that the arrow keys move at the original's 2 pixels a frame; Space or Enter click. A click on a drawer, found by the desk's colour under the pointer (11, 12 or 4), slides its first poster up, or its other one when the drawer's poster is open; a taken poster gives way to its pair. A click on the open poster takes the mission. The lights hold for half a second each way instead of the original's busy wait of about 1.5 s.
- **Station** (`station.cpp`): STATION.CPV under its own palette, no fade; the attendant (image 8 at 64,64) or, when robbed, the attendant tied up (image 1 at 256,96); the panel (image 2 at 160,4), FILL UP (image 3, greyed 6) at 168,13, REPAIR TYRE (4, greyed 7) at 168,26, EXIT (5) at 168,48; the cursor as two colour-11 lines per item (`stationCursorLines`, `0:39fe`), starting on EXIT. Up and Down move it one item per key press (the original repeats while the joystick is held); Space or Enter choose.
- **Endings** (`mission_end.cpp`): the picture of the ending, if any, faded in under its own palette, held until Space or Enter, faded out; then the score screen under `scorePalette` (split at row 168): GAME OVER (GAME_SCO image 1 at 32,20) except after an arrest, SCORE : (image 2 at 55,100), five digits (images 3-12 at 172 + 19 i, 100), and the ending's text in LETTRE1 on the 8 x 8 grid. Fades step every 1/16 s, twice the original's pace. Left out: "QUARTEX 1990!", the crack's line at (13,24); "ALL THE STATION HAVE BEEN ROBBED..." reads STATIONS.

The driving view is not ported yet; `build/hp2 --start station` or `--start ending --ending <reason>` open the other screens directly (`building.md`).

## Missions

`missionTable` (`1:23ae`), 6 entries `{type, target max speed, BCD bounty}`:

| Poster | Type | Target max speed | Bounty |
|---|---|---|---|
| 1, 2 | 0 pull over | 200, 250 | $2000 |
| 3, 4 | 1 roadblock | 300, 350 | $5000 |
| 5, 6 | 2 shoot | 350, 400 | $10000 |

The bounty is added to `bounty` (`1:236a`, BCD, doubles as the score). It drains by 1 every 6 frames (`frameMod6`), by 20 per vehicle hit or rule penalty, by 40 per bullet hitting the traffic car; ESC sets it to 0.

Arrest rules (`CheckArrest`, `0:4f14`; details in `vehicles.md` section 10):

- Type 0: siren on (key S), same cell as the target, target on screen, closer than 1000 units, heading difference under 90 degrees.
- Type 1: the target rams your car while you are stopped (`rammedWhileStopped`).
- Type 2: 5 gun hits on the target (key T toggles aiming; fire shoots). The target shoots back every 6th frame while in your cell; being hit ends the mission.

## The criminal

The target car (`1:3c7c`, AI record `1:3da8`) drives cell by cell (`PlanCellRoute`, `FollowCellPath` over `cellPathTables`) to the nearest station not yet robbed (`FindNearestStation`; Manhattan distance over `stationTable`, the 20 cells of types 11/12). It robs the station by stopping in the station lay-by while the player is more than one cell away (`stationsLeft` - 1), then heads for the next one. If the player is within one cell when it stops, it leaves without robbing; that branch writes both new target coordinates into the same field (`0:3100`), which may make it stop in a non-station cell later (*unverified* consequence). When `stationsLeft` reaches 0 the mission fails.

A traffic car (`1:3e0c`, AI record `1:3f38`) is spawned by `UpdateTrafficCar` in a neighbouring open cell, usually ahead of the player, heading for the cell beyond the player; it gets a random colour scheme (1-7; the criminal's car is scheme 0, red) and a cruise speed of 200 + 32 x (scheme - 1).

## Player resources

From the player record (`playerCar`, `1:3b50`; full field map in `vehicles.md`): fuel `+5a` (0xffff full, drops with speed squared), engine temperature `+6a` (rises while rpm >= 300), damage budget `+6c` (10000, reduced by impacts), tyres `+82` (2, one lost per spin-out).

Stopping inside a station lay-by opens the **station** scene (`StationScene`): FILL UP (fuel full), REPAIR TYRE (tyres back to 2), EXIT. Both are free and unavailable at a robbed station.

## Scenes and endings

| Event | Picture | Code |
|---|---|---|
| Spin-out after leaving the road at speed (tyres left) | PAGE_F1.CPV, game continues | `UpdateImpacts` `0:4774` |
| Out of fuel | PAGE_F3.CPV | reason 0x01 |
| All stations robbed | text "ALL THE STATION HAVE BEEN ROBBED..." | reason 0x02 |
| Engine overheated | PAGE_F4.CPV | reason 0x04 |
| Car wrecked | PAGE_F5.CPV | reason 0x08 |
| Tyres gone | PAGE_F2.CPV | reason 0x10 |
| Shot by the criminal | text "YOU HAVE BEEN SHOT..." | reason 0x20 |
| Arrest | PAGE_F6.CPV; "YOUR MISSION IS OVER..." after the last poster | reason 0x40 |
| Bounty at 0 (or ESC) | score only | reason 0x80 |

Every ending shows the score screen (GAME_SCO.IMG; "GAME OVER" except after an arrest) with "QUARTEX 1990!" printed at the bottom (*unverified* whether the crack replaced an original string). Failures clear the score.

## Keys

Joystick (port 2) or cursor keys: steer, accelerate (up), brake (down; at standstill it toggles reverse). Fire or space: fire the gun while aiming. S: siren. T: toggle aiming (`0:4d82`, KeyDown 0x54; the key table maps letters by their AZERTY position). P: pause. ESC: abandon (bounty 0).

## Copy protection

`ManualLookupProtection` (`0:bd4e`, unreferenced in this crack): a random map square's road type had to be picked from 12 tile icons (MODULE.IMG), three tries; it then offered "INSTRUCTIONS (Y/N)" showing HELP.CPV. LoadFile still checks the volume name "Highway Patrol" before every load.
