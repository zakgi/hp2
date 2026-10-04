# Renderer and world geometry

Evidence: Ghidra project `hp2`, program `hp.prg` (Quartex NTSC crack). Places in `hp.prg` are written `hunk:offset`. Statements are read from the code unless marked *unverified*.

## Screen layout

One 320x200 lowres screen, 4 bitplanes (16 colors), planes 8000 bytes apart, 40 bytes per row, double buffered (`frontScreen` displayed, `backScreen` drawn, swapped by `FlipScreens` at vblank). Palettes are installed as copper lists that can change colors partway down the screen (`InstallPalette`), so the view and the dashboard can use different colors.

| Rows | Content | Drawn by |
|---|---|---|
| 0-15 | Roof strip with HUD text (text at pixel row 4) | `DrawDashboard` (DES_TABB image 5), `DrawHudText` |
| 0-69 | Sky, color 8 | `ClearSkyRows` |
| ~50-69 | Horizon backdrop (DEC_FOND.IMG), scrolls with heading | `RenderRoadView` |
| 70-131 | Ground, color 14; road polygons set plane 0 (color 15); lane stripes color 1 | `ClearGroundRows`, `RenderRoadView` |
| 129-131 | Hood edge cut-outs, color 0 | `MaskBonnetEdge` |
| 132-199 | Dashboard (DES_TABB image 4, 320x68), gauges, hands on the wheel | `DrawDashboard` |

The horizon is row 70 (`ProjectPoint` adds 70). Sprites (cars, roadside objects, signs) are drawn after the road; see "Objects and sprites" below.

### Palette (`viewPalette`, `0:a520`)

Much of the look comes from the copper: one palette list with 39 segments.

| y | Colors |
|---|---|
| 0 | roof strip: 000 800 246 468 68a 8ac acf fff 222 a00 888 888 c00 600 f20 000 |
| 15 | view: 0 black, 1 white (stripes), 3 444, 8 sky 05f, 9 484, 10 aaa, 11 c00, 12-15 sand/asphalt; 4-7 = car colors (`SetCarColors`, 8 schemes in `carColorSchemes`; 0 = the criminal's red car, 1-7 traffic) |
| 20-68 | color 8 (sky) every 2 lines: 06f, 07f ... 0ff, 1ff ... fff (blue at the top, white at the horizon) |
| 70-92 | colors 12-15 every 1-4 lines, from fff fff fff fff at the horizon to 841 555 a63 555 near the car: distance haze on sand (12, 14) and asphalt (13, 15) |
| 132 | dashboard: 000 f84 246 468 68a 8ac acf fff 222 444 666 888 aaa 600 c00 a40 |

`viewPaletteRed` (`0:a734`) has the same layout in shades of red; it is shown for two frames after a car-to-car collision (`redFlash`) and while `playerShot` is set. The ground is color 14 (sand) and the road color 15 (asphalt), so the haze gradient shades both by screen row.

## Frame order (main loop at `0:ba7c`)

`ClearSkyRows`, `ClearGroundRows`, `ReadInput`, game logic, `BuildRoadPolygons` (`0:d9ec`), `RenderRoadView` (`0:dc36`), sprite and object passes, bullets (BALLE.IMG), `MaskBonnetEdge`, `DrawDashboard`, `DrawHudText`, palette update, sound flags, `FlipScreens`.

## World

- Map: CARTE.BIN, 64x64 bytes, index `y*64 + x`, one road cell type per byte (0-12). The used area is about x 1-39, y 1-38. `scripts/render_map.py` draws it.
- Cell size: 0x4000 world units. Positions are a cell (map x, y) plus a fine position 0..0x3fff inside it (player record `+0/+2` cell, `+4/+6` fine; field map in `vehicles.md`). Heading 0 = +x (east on the HUD compass), 180 = +y (north); motion is dx = v cos h, dy = v sin h.
- Angles: 720 steps per turn (half degrees). `cosTable` (`0:83a0`) and `sinTable` (`0:8940`) are Q14 (16384 = 1.0); `atanTable` (`0:8ee0`) holds atan(i/256) in half degrees for `Atan2`.
- Road cell shapes (`roadCellShapes`, `0:7812`): one closed polygon per type in cell-local x/z, road 1024 units wide centered on 8192:

| Type | Shape | Count on map |
|---|---|---|
| 0 | none (desert) | 3272 |
| 1 | straight, along z (N-S) | 44 |
| 2 | straight, along x (E-W) | 50 |
| 3-6 | quarter-circle curves, radius 8192 about a cell corner (52 edges) | 155, 159, 153, 168 |
| 7, 8, 9 | T junctions | 17, 24, 11 |
| 10 | crossroads | 23 |
| 11 | N-S road with a driveway loop on the east side | 8 |
| 12 | E-W road with a driveway loop on the north side | 12 |

Types 11/12 are the 20 stations (*unverified* link to "ALL THE STATION HAVE BEEN ROBBED...", string at `0:77d8`). Polygon format: word n (low byte used), then n+1 points of (x, z, y) words, the last equal to the first; y is 0 for all road shapes.

## Road pipeline

1. **Visible cells** (`SelectVisibleCells`, `0:db78`): the player's cell plus at most one neighbor. `visibleNeighbourTable` (`1:2bac`) is indexed by fine-y quadrant (inverted), fine-x quadrant and heading octant (`HeadingOctant` = heading/90 & 7) and gives 0 none, 1 +x, 2 +y, 3 -x, 4 -y. So the visible road never reaches beyond the adjacent cell (16384 units ahead at most).
2. **Transform** (`BuildRoadPolygons`, `0:d9ec`): for each visible cell, type from CARTE.BIN (0 off the map), `TranslatePolygon3D` by `cell*0x4000 - player fine position` (16-bit wrap), `RotatePolygon3D` by `0xb4 - heading` (mod 720) with x' = (x cos - z sin) >> 14, z' = (x sin + z cos) >> 14.
3. **Near clip** (`ClipPolygonNear`): against z >= 200, crossing found by bisection (`ClipEdgeNearBisect`).
4. **Projection** (`ProjectPoint`): sx = (x << 8) / z + 160, sy = ((y + cameraHeight) << 8) / z + 70. Focal length 256 pixels. `cameraHeight` is `1:2368` = player `+48`: 100 at rest, plus a 40-step suspension wobble of -6..+6 (x4 off the road) or a bump profile when hitting stones (up to +80); it also lowers the backdrop when >= 100.
5. **2D clip** (`ClipPolygon2D`): Sutherland-Hodgman against x 0..319, then y 70..131, intersections by bisection (`ClipEdgeX`, `ClipEdgeY`).
6. **Fill**: `DropDegeneratePoints`, then an XOR edge fill in `scratchBuffer` (one pixel per scanline per edge: `XorPolygonEdges`/`XorEdgeLine`; vertex parity fix: `FixPolygonVertices`; horizontal edges are filled directly with `FillHSpan`). `FillRoadRows` walks rows 70..131, turns the edge bits into filled spans with byte tables (`fillPrefixXor`, `fillPrefixXorInverted`, `fillParity`), ORs them into plane 0 of `backScreen` (ground color 14 + plane 0 = color 15) and records each row's span edges (up to two spans) in `roadSpanBuffer`.
7. **Lane stripes** (end of `RenderRoadView`): `stripePhase` += speed << 9 / 0x69 per frame (mod 0x1400; subtracted when the player's `+6e` reverse flag is set). Per screen row 76..131, `roadStripeTable[stripePhase >> 7][row - 76]` says whether a dash is visible (40 phases, dashes longer near the bottom). For every recorded span, a stripe `roadStripeWidths[row]` pixels wide (1 at the horizon, 16 at the bottom) is drawn just inside the left and right edges, color 1. The stripes are edge lines, not a center line.
8. **Backdrop**: DEC_FOND.IMG, image index from heading: `heading*16/6` split into image (quotient by 320) and x (remainder - 320); drawn twice so it wraps; y = 50, or 50 + (cameraHeight - 100)/10 when cameraHeight >= 100; clipped to rows < 70.

## Dashboard (`DrawDashboard`, `0:ce74`)

- Gauges from the player record: left needle from `+5a` high nibble (table `gaugeLeftNeedleTips`, origin (93,197)), warning glyph when the nibble is 0; right needle from `+6a` high nibble (`gaugeRightNeedleTips`, origin (226,197)), warning glyph at 0xf.
- Speedometer: angle 0x198 - speed*99/84, radius 20 around (131,179), drawn as three lines. Tachometer: angle 0x18c - rpm (`+5c`)*98/88 around (188,179). Left needle = fuel `+5a`, right needle = engine temperature `+6a`.
- Hands on the steering wheel: DES_TABB images 2 and 3 placed on a radius-93 circle at angles derived from `+0c` (steering wheel). Gun mode (`gunMode`, key T; `aimReturn`, `aimHandAngle`) swings the right hand off the wheel; in gun mode the sight (BALLE image 1) is drawn at `aimX/aimY` (fixed ahead, jittered with speed, bobbing with the camera), and the impact (image 3) when `shotHit`.
- While the car moves, DES_TABB image 1 alternates between y 133 and 132 each frame (`dashBlink`): a vibration effect.

## HUD text (`DrawHudText`, `0:e1b4`)

8x8 glyphs from LETTRE1.BIN / LETTRE2.BIN (94 glyphs of 32 bytes: 8 rows x 4 planes, ASCII 0x20-0x7d) drawn at pixel row 4: player map x and y (2 digits each, `+0`, `+2`), player compass (heading quantised to 8 directions, `compassGlyphs`), the same three for the record at `1:3c7c`, the BCD value at `1:236a` (4 bytes, adjusted with `abcd`/`sbcd` by `0:cd84`/`0:cdc8`), and 20 - `1:23de`.

## Objects and sprites

Scenery comes from COOR_OBJ.BIN (`formats.md`): per road cell type 80-107 objects `{x, y, z, type, extra}` in cell coordinates: 20 cacti, 20 bushes, 40 stones on every cell; signs on curves (types 8/10) and junctions (type 9); the station cells (11/12) replace them with a 45-cactus fence, 40 stones and two station signs (type 11). Every cell of a type has the same scenery.

Per frame (`BuildViewObjects`, `0:2452`), for each visible cell (the same one or two cells as the road): copy the cell type's list translated to the player (`CopyTranslateObjects`), insert the one other car shown (the criminal if in a visible cell, else the traffic car, type 2), rotate by 180 - view heading (`CopyRotateObjects`), collect the player's collision contacts (box x -120..220, y -200..speed+400, up to 8), cull (`CullObjects`: depth 300..0x2134, |x| < 4000), project (`ProjectObjects`, same projection as the road) and bubble-sort far to near.

`DrawObjects` (`0:26e8`) draws them in that order:

- Size: 10 pre-scaled frames per object (the .IMG banks), chosen from depth: l = max(0, depth - 256) >> 6, compressed to 0-9 (l < 4: l; l/2 < 4: l/2 + 2; else l/4 + 4) through per-type LOD tables (`carLodTable` for cars).
- Stones with `DrawSmallBob`, everything else with `BlitBob`; sprite y clamped to <= 0x8a so nothing covers the dashboard.
- Cars: 24 viewing angles from the car's heading relative to the view and its bearing; each angle maps to a VOITURE0-6 bank, angles 7-16 use horizontally mirrored frames (`MirrorBobFrame`, mirrored lazily in place, tracked in `carMirrorFlags`). The drawn car's bank, frame and screen position are kept for shooting (`drawnCar*`).
- Signs: 4 views (front, back, edge) from the angle between the view and the sign (`extra`), on PAN_POT poles; station signs add PST_STA on two poles.

Bullet holes (`bulletHoles`, BALLE.IMG) are drawn on top of the view afterward and stay for the rest of the mission.

## Notes for the port

- The view is 320x62 pixels of road under a sky, with at most two cells of road geometry. Higher resolution needs only the projection constants (focal 256, center 160/70) and the clip rectangle changed; the stripe tables are tied to 56 rows and need regenerating or replacing.
- All geometry is 16-bit fixed point; the XOR fill and the byte tables exist only for speed on the 68000.
