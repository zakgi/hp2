# Vehicle simulation: player car, suspect, traffic, collisions, shooting, math helpers

Source: work/listing/hp.lst (Ghidra listing), table dumps read from hp.prg hunks. Everything below cites places in hp.prg as `hunk:offset`; inferences are marked "unverified".

Conventions used in this file:
- "w" = 16-bit word, "l" = 32-bit long. All game arithmetic is 16-bit signed unless stated. Many comparisons use `bpl/bmi` (sign of the 16-bit difference) rather than `bge/blt`; a faithful port should do the same int16 wrap-around arithmetic.
- Angles are in half-degrees, 0..719 (0x2d0 = full turn, 0xb4 = 180 = 90 deg, 0x168 = 360 = 180 deg).
- "car1" = player record 1:3b50, "car2" = suspect record 1:3c7c (aux 1:3da8), "car3" = traffic car record 1:3e0c (aux 1:3f38).
- Several routines outside the task list had to be read to explain the control and AI models: 0:2dca (per-frame vehicle update), 0:3a2e/0:3d0c (drive physics), 0:3e7e + 0:1b56 (movement), 0:4068 (collision response), 0:49f8, 0:340a, 0:334c, 0:33da, 0:3f26/0:3f62, 0:5a44, 0:5c56, 0:5e92 (AI), 0:3fa8 (off-road test), 0:1eb2, and the head of 0:6008 (end-of-mission tests).

---

## 1. World coordinate system

- Position = cell (word cellX at +0x00, cellY at +0x02) + position inside the cell (word posX at +0x04, posY at +0x06), 0 <= pos < 0x4000. One cell is 0x4000 = 16384 units square. The global coordinate is `cell*0x4000 + pos`; 0:4a88 computes exactly this as a 32-bit value (0:4abc-0:4afa).
- CARTE.BIN (4096 bytes) is a 64x64 byte grid, row-major, `type = map[cellY*64 + cellX]` (0:2bc6, 0:316e, 0:3412, 0:5516, 0:5ab2, 0:da46). Only 40x40 is used: 0:3e7e clamps cells to 0..0x27 (0:3ed8-0:3f00); the map content occupies cells 1..38 (checked on the file).
- Cell types (byte): 0 = no road; 1..10 = road pieces; 11 (0xb) and 12 (0xc) = stations (road + station area). Exit bitmask per type (table 0:5eea, used by AI and traffic spawn): bit 1 = +x side, bit 2 = -x side, bit 4 = +y side, bit 8 = -y side:
  `type: 0:0xf 1:0xc 2:0x3 3:0xa 4:0x9 5:0x5 6:0x6 7:0x7 8:0xb 9:0xd 10:0xf 11:0xc 12:0x3` (13:0x5 and 14:0xa are pseudo-types used only by the suspect, see 9.1). Verified consistent with neighbouring cells in CARTE.BIN (e.g. cells (1,1)=5 and (2,1)=6 connect on x, (2,1) connects to (2,2)=10 on +y).
- Heading: angle 0 = +x, 180 (90 deg) = +y; motion is `dx = v*cos(h)`, `dy = v*sin(h)` (0:1b56 via 0:1bd2). The HUD compass (0:e1b4, index `(h+45)/90` into table 0:e5b0, font index = ASCII-0x20) prints 0 = "E", 1 = "NE", 2 = "N", 3 = "NW", 4 = "W", 5 = "SW", 6 = "S", 7 = "SE". So +x = east, +y = north, angles counter-clockwise; map row index grows northward (a north-up drawing must flip CARTE.BIN rows: unverified, depends on the other renderers).
- Cell-crossing code (0:3e7e 0:3ea0-0:3ed2): 1 = moved -x, 2 = moved +x, 4 = moved -y, 8 = moved +y. This equals the exit bit of the side through which the new cell was entered (moved +x = entered through the -x side = bit 2), which is how the AI uses it.
- Camera/view frame: objects are translated by `-player.pos` (+cell delta*0x4000, 0:da56-0:da66) and rotated by `180 - heading18` (0:2452 0:2574-0:25aa). In that frame x' = lateral (positive = right of view), y' = forward depth. The same rotation is used for every "relative position" test below.
- Projection (0:1d98): `sx = x'*256/y' + 160`, `sy = (z + camH)*256/y' + 70`, camH = 1:2368 = player +0x48 (100 + bounce). Visible objects: 300 <= y' < 0x2134 and -4000 <= x' < 4000 (0:23e2).
- Speeds are in units per frame. Player max 400 units/frame = one cell per ~41 frames. No real-world scale appears in the code (unverified).

## 2. Math helpers (exact register contracts)

| Address | Proposed name | In | Out / notes |
|---|---|---|---|
| 0:1bfa | CosSin | D0.w = angle 0..719 | D0.w = cos (table 0:83a0[angle]), D1.w = sin (table 0:8940[angle]); Q14 (16384 = 1.0). A0 preserved. No range check: angle outside 0..719 reads beyond the table. |
| 0:1bd2 | PolarToXY | D0.w = radius r, D1.w = angle | D0.w = (r*cos)>>14, D1.w = (r*sin)>>14. D2,D3,D5-D7 preserved. |
| 0:1d70 | RotateXY | D0.w = angle, D2.w = x, D3.w = y | D2.w = (x*cos - y*sin)>>14, D3.w = (x*sin + y*cos)>>14. D0,D1,D5-D7 preserved. |
| 0:1c1a | Atan2 | D0.w = x, D1.w = y | D0.w = angle 0..719 of vector (x,y). Uses |x|,|y|, ratio = (min<<8)/max (divu), table 0:8ee0[ratio] (half-degrees), then `180 - a` if |y|>=|x|, `360 - a` if x<0, negate if y<0, +720 if negative. All other regs preserved. |
| 0:1d98 | ProjectPoint | D0 = x, D1 = depth, D2 = z, D3 = height offset | D0 = (x<<8)/depth + 160, D1 = ((z+D3)<<8)/depth + 70 (divs, quotient word). D2 clobbered. |
| 0:1b56 | AdvanceInCell | D0 cellX, D1 cellY, D2 posX, D3 posY, D4 speed, D5 heading | PolarToXY(speed, heading) added to pos; one wrap per axis: <0 -> +0x4000 and cell-1, >=0x4000 -> -0x4000 and cell+1. Returns updated D0-D3, D4/D5 unchanged. |
| 0:22a4 | OutCode2D | D0.w = x, D1.w = y; box in globals 0:22f2 xmin, 0:22f6 xmax, 0:22f4 ymin, 0:22f8 ymax | D2.w bits: 1 x<xmin, 2 x>=xmax, 4 y<ymin, 8 y>=ymax; 0 = inside. Box is shared and rewritten by many callers. |
| 0:1eb2 | ClipEdgeNearBisect | (D0,D1) and (D2,D3) segment end points, plane y = (0:22f4) | Bisection until the midpoint's y-Y0 stops changing or is 0; returns D0 = x at crossing, D1 = Y0. Clobbers D4-D7. |
| 0:3c12 | WrapAngle | D0.w | loops +/-720 into 0..719 |
| 0:3c32 | ClampSpeed | A0 = vehicle | +0x08 clamped to 0..+0x70 |
| 0:3c54 | ClampSteer | A0 = vehicle | +0x0c clamped to -60..60 |

Tables (copy them from the binary, the generator is not exact):
- 0:83a0: 720 words, cos(i/2 deg)*16384 (value at 0 = 16384, at 180 = 0, at 360 = -16384). Not exactly round() nor trunc() of the float formula (about 500 entries differ from round()), so extract verbatim.
- 0:8940: 720 words, sin(i/2 deg)*16384, same remark.
- 0:8ee0: 257 words used. Entries 0..255 = trunc(atan(i/256)*360/pi) (verified, 0 mismatches). Entry 256 (address 0:90e0, a separate label) holds 0x00ff = 255, so an exact diagonal |x| == |y| returns a wrong angle (for x = y > 0: 180-255 = -75 -> 645 instead of 90). Atan2 of (0,0) executes `divu` by zero (68000 divide-by-zero exception). Both are original bugs.
- Shifts use `lsr.l` on signed products; the low word of the result equals an arithmetic shift, i.e. floor((a*b)/16384) truncated to 16 bits.

Random source: ReadBeamPosition (1:19a4) only. Uses here: car2 start cell `(D0 & 0xf0)>>4` (0:b1d0), player start `(D0 & 0x60)>>5`, 3 -> 0 (0:b65a), crosshair jitter (0:2f08), traffic colour `((D0 & 0xe)>>1)+1` (0:5732).

## 3. Main loop order (0:ba7c-0:bcc6), all per frame

1. 0:e084, 0:dfde (not analysed here), ReadInput.
2. If player +0x6e (reverse) != 0: `neg joyY` (0:ba8a-0:ba98).
3. 1:234e = 0; set 1:234e = -1 if player +0x26, +0x22, +0x2a are all 0 and joyY == -1 and speed (+0x08) == 0 (0:ba9e-0:bad8). Also while 1:22e2 != 2: 1:2350 = 0, 1:234e = -1, 1:22e2++ (0:bae0-0:bafa). See 7.3.
4. 0:4d82 ToggleGunMode, 0:4dca PlayerShoot, 0:50b6 SuspectShoot, 0:540e UpdateTrafficCar, 0:2dca UpdateVehicles, 0:4a88 CollidePlayerSuspect, 0:4c10 CollidePlayerTraffic.
5. 0:bb1e (re-entry point used by 0:2dca when the suspect has no waypoint left): 0:5064 ToggleSiren, 0:4f14 CheckArrest, 0:6008 (end-of-mission), 0:d9ec (visible cells/road geometry), 0:dc36 (road/background), 0:3fa8 CheckOffRoad, 0:2452 BuildViewObjects, 0:26e8 DrawObjects, 0:2bb2 SenseSuspectObstacles.
6. Bullet holes: for each of 0:53e2 entries in 0:53e4 (x,y words) BlitBob bank BALLE.IMG (1:27a0) (0:bb4a-0:bba8).
7. 0:d836, 0:ce74 (HUD/dashboard, not analysed). Every 6 frames (0:540c >= 6, counter incremented in 0:50b6): clear it and 0:cdc8(1) (BCD counter 1:236a -= 1) (0:bbb4-0:bbcc).
8. 0:e1b4 (HUD text), traffic colours 0:e5d0(1:2342) + SetPaletteColors, palette flashes (1:2344 while player +0x2a active, 1:2358 -> palette 0:a734).
9. Sound requests: engine always (0:1390), siren if 0:50b4, skid if |player +0x14| >= 50 and speed >= 100 (0:bc58-0:bc7c); 0:1162; FlipScreens.
10. 0:bcca: key 0x50 ('P') pause. Key 0x1b (ESC): 1:236a = 0 (forces end condition 0x80) and player speed = 0 (0:bcb2-0:bcc2).

Update rate: one simulation step per loop iteration. FlipScreens (1:0afa) waits for blitter idle and the next vblank, so the step rate is the display rate divided by the frames each iteration takes; nothing is time-scaled. The real frame rate is unverified (depends on rendering load; NTSC crack, 60 Hz VBL).

Order inside 0:2dca (0:2dca-0:334a):
1. Reverse toggle (7.3).
2. Player: 0:49f8 (classify sensed objects), 0:340a (station area flag). Same for car2. If car2 aux +0x0e == 0: car2 +0x50 = 0.
3. AI inputs: 0:3f26 (car2), 0:3f62 (car3, only if 0:5880).
4. Player inputs: +0x60 = joyX, +0x62 = joyY, +0x64 = fireButton; +0x62 = 0 if player +0x50 or 1:2356 != 0.
5. 1:2360 = 0; 0:4068(player); if 1:2360 -> crash sound 0:1398.
6. Camera: 1:2368 = player +0x48; crosshair y 1:233e = (+0x48-100)/2 + 80; crosshair x 1:233c = 160 + rnd(-15..15)*speed/400, folded into 130..190 (never triggers).
7. 0:4068(car2); 0:4068(car3) if active.
8. Player: +0x56 = 3, +0x58 = 5; 0:3a2e (drive); 0:3e7e (move); if cell changed 1:2340 = crossing code. If +0x6e: +0x18 -= 360 (wrapped).
9. car2: 0:3a2e -> aux +0x08 = D0 (always 0), aux +0x18 = yaw delta; 0:3e7e -> aux +0x00 = changed flag, aux +0x16 = crossing code if changed. Same for car3 with aux 1:3f38 if active.
10. Proximity flags: 1:2352 = same cell as car2, 1:2354 = |dcellX|<2 and |dcellY|<2 (both set when same cell).
11. Suspect waypoint logic (9.1). 12. Player station logic (7.6).

## 4. Initialisation (main 0:b17e-0:b8c0, plus 0:b8c0-0:ba76)

- 1:23de = 20 (waypoints remaining); waypoint visited flags (word +4 of each 6-byte entry at 1:23e0) cleared.
- Mission table 1:23ae, indexed by `(1:232a-1)*8` (1:232a = selected mission 1..6 written by 0:c3aa, unverified): {word class -> written back to 1:232a, word car2 +0x70, long BCD added to 1:236a via 0:cd84}:
  `m1: 0,200,0x2000  m2: 0,250,0x2000  m3: 1,300,0x5000  m4: 1,350,0x5000  m5: 2,350,0x10000  m6: 2,400,0x10000`.
  After this 1:232a means mission class 0/1/2 (section 10).
- car2 (1:3c7c): start cell = table 1:236e[(rnd & 0xf0)>>4], 16 (cellX,cellY) words: (25,5)(3,6)(12,6)(27,6)(30,7)(16,8)(33,14)(30,20)(21,21)(12,24)(16,26)(35,27)(10,29)(31,30)(30,32)(14,35); all are type-10 cells. pos (0x2000,0x3830), heading 0x21c (270 deg = south), speed 0. First waypoint = 0:334c -> aux +0x12/+0x14.
- player (1:3b50): start from table 0:b76e[rnd 0..2], 12 bytes {cellX, cellY, posX, posY, heading, 1:2340}: (11,15,0x07d0,0x2000,0,2), (24,11,0x2000,0x07d0,0xb4,8), (25,28,0x2000,0x3830,0x21c,4). +0x18 = heading.
- car3 (1:3e0c): cell (2,2), pos (0x2000,0x2000), inactive (0:5880 = 0).
- Initial values (player / car2 / car3): +0x52 accel 10/5/5, +0x54 5/5/2, +0x56 3/2/2, +0x58 5/6/6, +0x70 400/table/400, +0x6c 10000/32000/-1, +0x7a 4000, +0x7c 5, +0x7e 20, +0x48 100, +0x5a -1 (0xffff), +0x5e 1, +0x82 2, all other state words 0.
- aux car2 (1:3da8): +0x00 -1, +0x02 0, +0x06 1, +0x08 0, +0x0a 100, +0x0e 0, +0x10 2, +0x16 4, +0x18 0, +0x1a = car2 +0x70. aux car3 (1:3f38): same but +0x12 4, +0x14 1, +0x16 2, +0x1a 200.
- Globals cleared: 1:2336/1:2338 (gun), 1:233c = 160, 1:233e = 80, hit counters 0:4f0e/f10/f12, 1:2342 (traffic colour), 1:2344, 1:2348 = 5, 1:234c, 1:2356 (end reason), 1:2358/1:235a/04/06, 1:2352/1:2354, 0:5880, 0:50b4 (siren), bullets 0:53e2/0:53e4[10], 0:540c, 1:22e0.

## 5. Vehicle record (0x12c bytes each: 1:3b50, 1:3c7c, 1:3e0c)

R = read, W = write. "AI" = car2/car3.

| Off | Size | Meaning, units/range | Writers / readers (main ones) |
|---|---|---|---|
| +0x00 | w | cellX 0..39 | init; 0:3e7e W (clamped); 0:540e W (car3 spawn); read everywhere (map lookups, proximity, HUD 0:e1bc) |
| +0x02 | w | cellY 0..39 | same |
| +0x04 | w | posX in cell 0..0x3fff | 0:3e7e W; collision push-back W (0:42c4, 0:4428, 0:45a6, may go out of range until next move); station exit W (0:328e-0:331e) |
| +0x06 | w | posY in cell | same |
| +0x08 | w | speed, units/frame, 0..+0x70 | 0:3a2e (accel/brake/clamp, slip scrub), 0:4068 (collision decay), 0:2dca (station brake), 0:5c56 (AI brake), 0:4a88 (set to car speed/2), 0:4f14, 0:6008, ESC. Read by sound (0:1314: period 0x280-speed), HUD, road scroll (0:dd5e) |
| +0x0a | w | unused | - |
| +0x0c | w | steering wheel, -60..60, positive = left (CCW) | 0:3a2e; collision handlers (x1.5 bush, x4 crash); read by 0:5e92, HUD (0:d3aa, 0:d684, 0:d698) |
| +0x0e | w | steering frozen at start of a bump (channel A) | 0:4068 0:4212; read by 0:3a2e 0:3ab4 |
| +0x10 | w | travel heading 0..719 | 0:3a2e/d0c, collision handlers, reverse toggle (+360), station exit; read by 0:3e7e (motion), HUD compass, collision tests |
| +0x12 | w | unused | - |
| +0x14 | w | slip / body angle offset (half-deg). Signed in 0:3d0c, but collision handlers keep it mod 720 | 0:3d0c, 0:4068; main loop skid sound (|+0x14| >= 50) |
| +0x16 | w | unused | - |
| +0x18 | w | visual/body heading = (+0x10 + +0x14) mod 720; player in reverse: minus 360 | 0:3a2e 0:3b0e; read by camera rotation (0:2574, 0:dac2), background scroll (0:def8), visible cells (0:db7e), sprite view (0:2b04), obstacle sensing rotation |
| +0x1a | w | unused | - |
| +0x1c | w | number of sensed objects in +0x86 list (0..8) | 0:2452 (player), 0:2bb2 (car2) W; 0:49f8 R |
| +0x1e | w | channel A state (stone bump): 0 idle, -0x100 active, 0xff done | 0:4068; 0:3a2e (uses +0x0e when -0x100); 0:540e clears (car3) |
| +0x20 | w | channel A trigger: object type 3..6 in sensing box | 0:49f8 |
| +0x22 | w | channel B state (type 1 = cactus) | 0:4068; forced -0x100 by PlayerShoot (0:4ec4, 0:4ef6); main loop/0:2dca read (blocks reverse toggle / station) |
| +0x24 | w | channel B trigger | 0:49f8 |
| +0x26 | w | channel C state (types 8..11 = signs) | 0:4068 |
| +0x28 | w | channel C trigger | 0:49f8 |
| +0x2a | w | channel E state (car-car collision) | 0:4068; main loop palette flash |
| +0x2c | w | channel E trigger | 0:4a88, 0:4c10 |
| +0x2e | w | channel D state (type 7 = bush) | 0:4068 |
| +0x30 | w | channel D trigger | 0:49f8 |
| +0x32 | w | channel A frame counter (1-based) | 0:4068 |
| +0x34/+0x36 | w,w | channel B contact point (lateral x, forward y) in car frame; sign of x picks spin direction | 0:49f8; PlayerShoot sets 100/0 |
| +0x38 | w | channel B spin state: 0 = first frame pending, 0xff = contact x >= 0, -0x100 = x < 0 | 0:4068; PlayerShoot sets -0x100 |
| +0x3a | w | channel B shake toggle | 0:4068 |
| +0x3c/+0x3e | w,w | channel C contact point | 0:49f8 |
| +0x40 | w | channel C spin state | 0:4068 |
| +0x42 | w | channel C shake toggle | 0:4068 |
| +0x44 | w | channel E spin state | 0:4068 |
| +0x46 | w | channel D frame counter | 0:4068 |
| +0x48 | w | body height, 100 = rest; + suspension wobble / bump profile / shake. Player value is the camera height and moves the crosshair | 0:4068 W; 0:2dca R; car3 object z = 100 - +0x48 (0:2d66); SuspectShoot uses car2 +0x48 |
| +0x4a | w | wobble phase 0..39 | 0:4068 |
| +0x4c | l | pointer to current bump profile (0:4980 table) | 0:4068 |
| +0x50 | w | "inside station area" flag (-1) | 0:340a W; 0:2dca, 0:5c56 R/clear |
| +0x52 | w | brake/accel parameter: accel = +0x52 + +0x54, brake = 2*+0x52 per frame | init |
| +0x54 | w | extra accel | init |
| +0x56 | w | steering rate per frame | init; player re-set to 3 each frame (0:2fae) |
| +0x58 | w | extra steering rate when the wheel is past 10 on the other side | init; player re-set to 5 |
| +0x5a | w | fuel, unsigned, 0xffff full; -= ((speed>>3)^2)>>6 per frame | 0:3a2e; station refuel (0:389a); HUD (0:cfa2, 0:cfdc); 0 -> end bit 1 (0:6034) |
| +0x5c | w | engine RPM (gear/speed table) | 0:3a2e; HUD 0:d216 |
| +0x5e | w | gear 1..5 (automatic) | 0:3a2e |
| +0x60 | w | steering input -1/0/+1 (joyX convention: +1 right) | 0:2dca (player), 0:3f26/62 (AI) |
| +0x62 | w | throttle input -1/0/+1 (+1 accelerate) | same; zeroed by collision handlers, station, end of mission |
| +0x64 | w | fire input (player fireButton; AI: "large steering error" flag) | read into D2 by 0:3a2e but never used (effectively unused) |
| +0x66 | w | rough-ground flag (player: pixel test; AI: lateral outside lane band) -> wobble x4 | 0:3fa8, 0:403e, 0:5c56; 0:4068 R |
| +0x68 | w | off-road counter (player): +2/frame off road at speed >= 100; >= 600 -> crash; halved when back on road | 0:3fa8 |
| +0x6a | w | engine temperature 0..0xffff: +100/frame while RPM >= 300 else -100 | 0:3a2e; HUD (0:d01a, 0:d058); 0xffff -> end bit 4 |
| +0x6c | w | damage budget (signed). Player 10000; < 0 -> end bit 8. AI values unused | 0:4068 decrements |
| +0x6e | w | reverse flag (player) | 0:2dca toggle; main loop; 0:3a2e; 0:4068; road scroll 0:dd6c |
| +0x70 | w | max speed (player 400 forward / 100 reverse) | init, reverse toggle, station exit |
| +0x72 | w | channel E shake toggle | 0:4068 |
| +0x74 | w | unused | - |
| +0x76 | w | channel E spin direction (player -1/0, other car +1/0) | 0:4a88/0:4c10 |
| +0x78 | w | unused | - |
| +0x7a | w | skid threshold on speed*yaw (4000) | init |
| +0x7c | w | skid divisor (5) | init |
| +0x7e | w | slip speed-scrub divisor (20) | init |
| +0x80 | w | crash in progress (player, from off-road); yaw x4 | 0:3fa8, 0:4068 |
| +0x82 | w | tyres (player, init 2); -1 per off-road crash; station "TO REPAIR TYRE" sets 2; 0 -> end bit 0x10 (PAGE_F2, burst tyre) | 0:3fa8, station 0:38ca, 0:6008 |
| +0x84 | w | crash shake toggle | 0:3fa8, 0:4068 |
| +0x86 | 8 x 6 | sensed objects {w type, w x (lateral), w y (forward)} in the car's view frame | 0:2452 / 0:2bb2 W; 0:49f8 R |
| +0xb6..+0x12b | | unused | - |

## 6. AI aux record (0x64 bytes reserved; 1:3da8 for car2, 1:3f38 for car3)

| Off | Size | Meaning | Writers / readers |
|---|---|---|---|
| +0x00 | w | replan flag (-1): set when the car changed cell (0:3e7e result, 0:3010/0:304e) or got a new target | 0:5a44 runs only when set; 0:5c56 clears it and resets +0x06 = 1 |
| +0x02 | l | pointer to the path polyline for the current cell (from 0:9ae0) | 0:5a44 |
| +0x06 | w | current segment index, 1-based | 0:5c56 |
| +0x08 | w | 0:3a2e D0 result (always 0): "stop" test in 0:5c56 is dead | 0:2dca |
| +0x0a | w | speed target while correcting lane position (100) | init/spawn |
| +0x0c | w | cruise speed target = +0x1a, or +0x1a/2 inside the target cell | 0:5a44 |
| +0x0e | w | arrived (car is in the target cell) | 0:5a44 W; 0:2dca (car2), 0:5c56 (car3) |
| +0x10 | w | segment index at which to stop when arrived (2) | init |
| +0x12/+0x14 | w,w | target cell X/Y | 0:334c result, 0:540e |
| +0x16 | w | entry side / crossing code (1,2,4,8) | 0:3e7e result; car2 new target (0:31a2..0:31c2); car3 spawn table |
| +0x18 | w | last yaw delta (0:3a2e D1); not read in the analysed code | 0:2dca |
| +0x1a | w | base cruise speed (car2 = mission max speed, car3 = 200 + 32*(colour-1)) | init, 0:540e |

## 7. Player control model

### 7.1 Inputs
- joyX (+1 right, -1 left) -> +0x60; joyY (+1 up, -1 down; negated in reverse) -> +0x62; fireButton -> +0x64 (unused by physics; shooting reads fireButton directly).
- Keys: 'T' (0x54) toggles gun mode (0:4d82), 'S' (0x53) toggles the siren (0:5064), 'P' (0x50) pause, ESC (0x1b) abort. Each toggle is edge-triggered by a latch (0:4dc8, 0:50b2, 0:bd4c).
- Throttle input is forced to 0 while in a station area (+0x50), while an end-of-mission sequence runs (1:2356 != 0), and every frame of any collision channel or crash.

### 7.2 Drive physics, 0:3a2e (A0 = vehicle), called once per frame
1. Throttle (+0x62):
   - +1: `speed += +0x52 + +0x54`, capped at +0x70 (player: +15/frame to 400) (0:3c76).
   - -1: `speed -= 2*+0x52`, floored at 0 (player: -20/frame) (0:3c96).
   - 0: speed only clamped to 0..+0x70 (0:3c32). There is no rolling drag: speed is held.
2. Steering (+0x60), wheel +0x0c:
   - +1 (right): `s -= +0x56; if s >= 10 then s -= +0x58; s -= +0x56`, floor -60. Player: -6 per frame, -11 when coming back from >= 13.
   - -1 (left): `s += +0x56; if s < -10 then s += +0x58; s += +0x56`, cap 60 (player +6, or +11 when s+3 < -10).
   - 0: self-centring by `+0x56*speed/200` per frame toward 0 (stops at 0), then clamp +/-60. No centring at speed 0.
3. Yaw: `yaw = speed * s / (2*speed + 512)` (divs; s = +0x0e during an active bump), negated in reverse (+0x6e), x4 during a crash (+0x80). `heading(+0x10) = wrap(heading + yaw)`. Example: full lock at 400 -> 18 half-deg (9 deg) per frame; at 100 -> 8.
4. Skid/slip (0:3d0c), skipped while channel B, C or E is active:
   - `excess = |low16(speed*yaw)| - +0x7a` (4000).
   - excess > 0: `d = -sign(speed*yaw)*excess/+0x7c` (/5); if |d| >= |yaw| then d = yaw; if d == 0 then d = sign(slip); `slip(+0x14) += d`. Full-lock skidding starts around speed 263.
   - excess <= 0 (recovery): if slip >= 10 (or <= -10): `heading += slip/20; slip -= slip/10`; if |slip| < 10: `heading += slip; slip = 0` (and skip the scrub).
   - Scrub (whenever slip remains): if speed >= 20 `speed -= |slip|/+0x7e` (/20); if speed < 20 or the result is negative: `speed = 0; heading += slip; slip = 0`.
5. `+0x18 = wrap(heading + slip)` (visual heading).
6. Fuel: `+0x5a -= ((speed>>3)^2)>>6` (unsigned, floor 0). 0..39 per frame.
7. Gear (+0x5e), only when speed != 0: down one if `speed <= (gear-1)*80` (floor 0), up one if `speed > gear*80` (cap 5); at most one change per frame.
8. RPM (+0x5c) from table 0:3bec, 4 words per gear {base, mul, add, pad}: `rpm = ((speed-base)*mul)>>3 + add` (mulu, lsr.w):
   gear1 (0,24,0), gear2 (80,15,130), gear3 (160,13,170), gear4 (240,11,210), gear5 (320,15,250). With a lagging gear (speed below base) mulu gives garbage for that frame.
9. Temperature (+0x6a): +100 if rpm >= 300 (cap 0xffff) else -100 (floor 0). Sustained speed above ~347 in 5th overheats.
10. Return D0 = 0, D1 = yaw.

### 7.3 Reverse gear and the 1:234e flag
- 1:234e = "toggle travel direction this frame" request. Main loop sets it when the (already sign-corrected) stick is pulled back, speed == 0 and no collision channel B/C/E is active (0:ba9e-0:bad8).
- 0:2dca (0:2dca-0:2e5c): the toggle happens only if 1:2350 (latch) is clear; it then sets the latch. The latch is cleared whenever speed != 0, so one toggle per stop.
  - entering reverse: +0x6e = -1, +0x70 = 100, heading += 360 (car now "drives forward" along the flipped heading);
  - leaving reverse: +0x6e = 0, +0x70 = 400, heading += 360.
- In reverse: main loop negates joyY (pull back = accelerate, push = brake, push while stopped = back to forward), 0:3a2e negates the yaw (0:3ab8, 0:3aca), 0:4068 negates +0x0c on entry and restores it on exit, after the move +0x18 -= 360 so the camera keeps looking the original way, and the road texture scrolls backwards (0:dd6c).
- During the first two frames of a new game (1:22e2 < 2, cleared only at 0:b132) the main loop forces 1:234e = -1 and clears the latch, so the toggle fires twice and cancels out (net effect: +0x70 re-set to 400). Purpose unverified.

### 7.4 Movement, 0:3e7e
AdvanceInCell(cell, pos, speed, +0x10); clamp cells 0..39; returns D0 = -1 and D1 = crossing code when the cell changed, else D0 = 0. Note: the clamp keeps the cell at 0/39 while pos wraps (edge behaviour unverified; the map has no road there).

### 7.5 Camera bounce
0:4068 (when channel A is idle): `+0x48 = 100 + wave[+0x4a]` (x4 if moving and +0x66), wave = 40 words at 0:4930: 0,1,2,3,4,4,5,5,6,6,6,6,5,5,4,4,3,2,1,0,0,-1,-2,-3,-4,-4,-5,-5,-6,-6,-6,-6,-5,-5,-4,-4,-3,-2,-1,0. Phase += max(1, speed/50) per frame while moving, reset at 40.

### 7.6 Stations (cell types 11/12)
- 0:340a (A0 = vehicle): +0x50 = -1 when the car is inside the station rectangle of its cell: type 11: x in [0x2400,0x4000), y in [0x900,0x33e4); type 12: x in [0x900,0x33e4), y in [0,0x1c00) (OutCode box).
- Player (0:31c8-0:3344): while +0x50, throttle = 0 and, unless a collision channel is active, `speed -= speed/5` (all of it if < 5). At speed 0: 0:349c (station screen; menu option 0 refuels +0x5a = 0xffff at 0:389a, option 1 repairs the tyres, +0x82 = 2 at 0:38ca), then all collision states, +0x6e, +0x80, +0x68, +0x6a cleared, +0x70 = 400, speed/steer/slip/+0x18 = 0, and the car is placed at the exit: type 11: posY < 0x2000 -> (0x2300,0x3500) heading 0x10e, else (0x2300,0x0b00) heading 0x1c2; type 12: posX < 0x2000 -> (0x3500,0x1d00) heading 0x5a, else (0x0b00,0x1d00) heading 0x10e. Then the loop restarts at 0:ba7c (stack popped).

## 8. Collisions and damage

### 8.1 Obstacle sensing (who touches what)
- Player: 0:2452 transforms the objects of the visible cells into the view frame; for the first slot only (the player's own cell, 1:2ba2 = (0,0)) it collects up to 8 objects with OutCode == 0 against box x in [-120,220), y in [-200, speed+400) into +0x1c/+0x86 (0:25bc-0:262c). Rotation uses +0x18, so while reversing the box still points along the view (quirk).
- car2: 0:2bb2 does the same with only car2's own cell, translated by -car2.pos, rotated by 180 - car2 +0x18, box y in [-200, speed+200).
- car3 has no scenery sensing (0:49f8 is not called for it).
- 0:49f8 (next frame, start of 0:2dca) clears triggers +0x20/+0x24/+0x28/+0x30 and sets: type 3..6 -> +0x20; type 1 -> +0x24 and contact (+0x34,+0x36), +0x38 = 0; type 7 -> +0x30; type 8..11 -> +0x28 and contact (+0x3c,+0x3e), +0x40 = 0.
- Object types (COOR_OBJ.BIN entries {x, y, z, type, extra}, bank table 0:7fa4): 1 = CACTUS.IMG (20 per road cell, 45 forming the station fences), 2 = car (inserted at run time), 3..6 = CAILLOUX.IMG stones (frames 1-3/5-7/9-11/13-15), 7 = BUISSON.IMG bush, 8/9/10 = road signs (front PAN_GAU / PAN_CRO / PAN_DRO, back PAN_ARR, edge PAN_PRO, plus a PAN_POT pole), 11 = station sign (PST_FLG/PST_FLD/PST_PRO plus PST_STA with two poles).

### 8.2 Response state machine, 0:4068 (A0 = vehicle)
Each channel: state 0 = idle; trigger seen -> state -0x100 (active); handler runs every frame while -0x100; handler sets 0xff when done ("wait for release"), 0xff -> 0 when the trigger clears. All channels are processed in one call in the order A, B, C, D, E, crash. Collision handlers zero the throttle input.

- A, stones (+0x1e, trigger +0x20): first frame `+0x6c -= speed`, +0x0e = steer, profile = 0:4980 + min(speed/100,3)*30 (word n, then n height offsets). Each frame: `+0x48 = 100 + profile[k]`, `speed -= speed/20` if speed >= 10, k++; done after n frames. Profiles: n=4 (0,20,0,-5); n=7 (0,40,40,20,0,-10,4); n=10 (0,30,60,60,30,0,-15,-6,6,-6); n=13 (0,40,70,80,80,75,60,40,0,-20,-8,8,-8). Steering for yaw is frozen (+0x0e).
- B, cactus (+0x22/+0x24/+0x38/+0x3a) and C, signs (+0x26/+0x28/+0x40/+0x42), same code: first frame: crash sound (1:2360), `+0x6c -= 2*speed`, push back `pos -= PolarToXY(2*speed+30, heading)`; if contact x >= 0: heading += 90, slip -= 90, state 0xff, else heading -= 90, slip += 90, state -0x100 (both wrapped mod 720). Each frame: with d7 = speed/8, d6 = speed/12: state 0xff -> slip -= d7, heading -= d6; else slip += d7, heading += d6. If speed >= 20: `speed -= speed/8`, +0x48 += or -= speed/10 alternately (shake). Else: speed = 0, heading += slip, slip = 0, channel state = 0 (not 0xff).
- D, bush (+0x2e/+0x30/+0x46): 2 frames of `+0x6c -= speed`, steer += steer/2 (clamped +/-60).
- E, car contact (+0x2a/+0x2c/+0x44/+0x72, side +0x76): first frame: crash sound, `+0x6c -= 4*speed`, 0:cdc8(0x20) (penalty, applied once per involved vehicle), 1:2348 = 0, 1:2344 = -1 (palette flash), push back as B, +/-90 turn by sign of +0x76. Each frame: slip +/-= speed/8 (heading unchanged), decay as B (speed/8, shake speed/10, 0:4906 ends the flash after 2 calls), end when speed < 20 (heading += slip).
- Crash (+0x80 == -1, player only): each frame `+0x6c -= speed`, throttle 0, +0x48 +/-= speed/8 alternately, speed -= 4. At speed <= 0: +0x80 = +0x68 = speed = 0; if tyres +0x82 != 0 and no end sequence: show PAGE_F1.CPV, WaitFire, restart the loop (car not moved); if +0x82 == 0 the end check (0x10) fires.

### 8.3 Car-car collision, 0:4a88 (player vs car2, only if 1:2354) and 0:4c10 (player vs car3, only if 0:5880)
1. Clear +0x2c on both cars.
2. Relative position of the other car in the player's travel frame: rotate (other.global - player.global, low 16 bits) by 180 - player +0x10 -> (lat, fwd); bearing = Atan2(lat, fwd).
3. Predicted position = (lat, fwd) + PolarToXY(other.speed, other.h - player.h + 180).
4. Collision if the predicted or the current point is inside x in [-120,220), y in [-200, player.speed+400).
5. On collision: both +0x2c = -1; player +0x76 = -1 if player.h < bearing (raw word compare) else 0; other +0x76 = negated (+1/0, so it always spins the same way: quirk); if player.speed < other.speed: player.speed = other.speed/2. a88 only: 1:235e = -1 (sticky "touched the suspect"), 1:22e0 = -1 if the player was stopped else 0. c10 only: 1:235c = -1 (never read).
car2 and car3 never collide with each other.

### 8.4 Off-road test, 0:3fa8 (player, after the road is drawn, before objects)
Reads plane 0 of backScreen at byte 0x1484 and 0x1498, bit 0x800 = pixels (100,131) and (260,131). Both set = on road: if +0x66 was set, +0x68 >>= 1 and +0x66 = 0. Otherwise, if not crashing: 0:403e (sound flag 0:13c4 on the first off-road frame at speed >= 100), +0x66 = -1, and if speed >= 100: +0x68 += 2; at 600: tyres -1, +0x80 = -1 (crash), crash sound, steer x4, +0x84 = +0x68 = 0. A port needs an equivalent "road under the two wheel probes" test (the meaning of plane-0 colours is unverified).

### 8.5 End-of-mission reasons (head of 0:6008, 0:600e-0:60da), 1:2356 bits
1: fuel +0x5a == 0; 2: no waypoint left (1:23de == 0); 4: temperature +0x6a == 0xffff; 8: damage +0x6c < 0; 0x10: tyres +0x82 == 0; 0x80: BCD counter 1:236a == 0; 0x40: 1:235a (arrest success); 0x20: 1:2358 (player shot). Then both cars lose throttle, car2 copies the player's speed, both slow by 4/frame until the player stops (unless crashing) and the page for the lowest set bit is shown.

## 9. AI

### 9.1 Suspect (car2)
- Goal: visit the 20 station cells listed at 1:23e0 (6 bytes each {cellX, cellY, visited}); all are type 11/12 (checked against CARTE.BIN): (4,1)(32,5)(13,6)(23,6)(10,11)(38,12)(26,13)(3,16)(17,16)(30,18)(11,19)(22,21)(6,26)(26,26)(19,27)(33,32)(2,33)(10,34)(38,37)(25,38). The HUD shows 20 - 1:23de (0:e558).
- 0:334c: nearest unvisited waypoint to car2's cell by Manhattan distance (ties: last in table); D2 = -1 if 1:23de == 0 or none left. 0:33da: mark (D0,D1) visited (if not found it writes past the table, at 1:245c, inside the CARTE.BIN filename string).
- Per frame (0:2dca 0:30ce-0:31c2), when aux +0x0e (arrived) is set:
  - player within one cell (1:2354): pick a new nearest target without marking the old one; aux +0x12 = D0 then aux +0x12 = D1 (bug: +0x14 not updated, target becomes (newY, oldY)); replan.
  - else, once car2 has stopped (speed 0): mark the target visited, 1:23de -= 1, pick the next; if none: jump to 0:bb1e (rest of the frame skipped; next frame ends the mission with bit 2). Else set the target, replan, +0x50 = 0, and aux +0x16 = 1/2 (type-11 cell, by sign of dx) or 4/8 (by sign of dy) to choose the leaving direction.
- Inputs come from 0:3f26: 0:5a44 (route) then 0:5c56 (follow); if 0:5886 is set, throttle = -1 and fire = -1.
- Special case in 0:5a44: for car2 only, cells (2,2) and (3,2) are treated as types 13 and 14.

### 9.2 Route choice per cell, 0:5a44 (A0 car, A1 aux), only when aux +0x00 != 0
1. aux +0x0e = arrived (cell == target); aux +0x0c = +0x1a/2 if arrived else +0x1a.
2. exits = table 0:5eea[type]; entry = aux +0x16; desired D4 = (dx>0 ? 1 : dx<0 ? 2 : 0) | (dy>0 ? 4 : dy<0 ? 8 : 0).
3. If D4 == 0 (in target cell): type 11 -> D4 = 1 and exits ^= 1; type 12 -> D4 = 8 and exits ^= 8 (drive into the station).
4. choice = D4 & exits & ~entry; if one bit, take it; if two, keep the x bits when |dx| > |dy| (cmp |dy| < |dx|) else the y bits.
5. If none: choice = exits ^ entry; if not a single bit: prefer the y axis when the desired direction has no y part, the x axis when it has no x part, otherwise the entry axis (straight on); then if still ambiguous `&= 0xa` (-x / -y preferred).
6. index = idx[choice]*4 + idx[entry], idx = {+x:0, +y:1, -x:2, -y:3} (byte table 0:5f08); path id = byte 0:5f18[type*16 + index]; aux +0x02 = long 0:9ae0[id-1].
Path data (24 paths, 0:9b40-0:a51f): word n, then n segments of 5 words {x0, y0, x1, y1, angle} in cell coordinates. Straights: path 1 = x 0x2200, y 0 -> 0x4000, angle 0xb4; path 2 = x 0x1e00 southbound; 3/4 = east/westbound on y 0x1e00/0x2200. Curves have 25 segments; junction turns 3 or 4. The 15x16 index table and the paths should be extracted verbatim.

### 9.3 Path following, 0:5c56 (A0 car, A1 aux) -> D0 joyX, D1 joyY, D2 flag
- If car +0x50 (in station area), or (car3 and arrived): brake `speed -= speed/5` (all if < 5); the returned D0/D1/D2 are whatever is in the registers (not +/-1 in practice; port as neutral).
- If aux +0x00: clear it, segment = 1.
- Advance segment while the car's progress along the segment exceeds its length (both measured after RotateXY by -angle).
- lateral = component of (car.pos - p0) left of the segment direction. Rough flag +0x66 = 0 only if 0 <= lateral < 0x400.
- lateral < 256 (right of lane): target = min((1280-lateral)/25, 180) (turn left); error = target - (heading - angle); coarse steer; speed target aux +0x0a (100).
- lateral >= 512: target = -min((lateral+256)/25, 180); same.
- 256 <= lateral < 512 (in lane): if arrived and segment >= aux +0x10: D0 = 0, D1 = -1, D2 = -1 and speed -= 10. Else error = -(heading - angle), fine steer, speed target aux +0x0c.
- (heading - angle) is wrapped only from above (>= 360 -> -720); errors below -360 are not wrapped (quirk).
- Throttle: +1 if speed < target, -1 if above, 0 if equal.
- Lane: the band 256..512 left of the polyline puts northbound cars at x 0x2000..0x2100 (east of the cell centre), i.e. right-hand traffic.

### 9.4 Steering controller, 0:5e92 (D1 = heading error, D4 = 0 coarse / -1 fine)
`s* = (err<<9)/speed`, then >>1 (fine) or >>2 (coarse). D0 = 0 if steer == s*, +1 (steer right) if steer > s*, else -1. D2 = -1 if |steer - s*| >= +0x56 + +0x58. Speed 0 -> D0 = D2 = 0. The AI then goes through the same 0:3a2e as the player (car2: +10/frame accel, -10 brake, steer 2/6; car3: +7, -10).

### 9.5 Traffic car (car3), 0:540e
- 0:5884 = player within 2 cells of car2 (|d| < 3 on both axes); 0:5882 = player within 1 cell of car3.
- Near the suspect: 0:5886 = 0; if car3 is active and was drawn this frame (0:2d90) with its sprite position (1:2332, 1:2334) outside [0,0x13f)x[0,0xc7): 0:5886 = -1 (both AI cars brake); otherwise car3 is deactivated (0:5880 = 0, colour 1:2342 = 0). Intent unverified.
- Near an active car3: when car3 reaches its target cell, new target = clamp(2*player.cell - car2.cell, 1..38); replan.
- Otherwise spawn (every frame until it succeeds):
  - quadrant q = ((player.h + 90) mod 720)/180, facing side bit = {1,4,2,8}[q] (0:5888). Player cell type 0 -> no spawn.
  - candidates = exits[type] & ~1:2340 (side the player entered by); one bit -> that neighbour; else `&= facing bit`; else `&= 0xa`; else `&= 2`; fallback +x. Bit 1 -> cellX+1, 2 -> cellX-1, 4 -> cellY+1, 8 -> cellY-1.
  - car3: that cell, speed 200, steer/slip/+0x18 = 0; {posX, posY, heading, aux +0x16} = 0:58a2[L*0x68 + spawnCellType*8], L = {0,0,2,0,1,0,0,0,3}[facing bit] (0:5890). Four entry kinds: (0x2100,0x03e8,0xb4,8) northbound, (0x3c18,0x2100,0x168,1) westbound, (0x1f00,0x3c18,0x21c,4) southbound, (0x03e8,0x1f00,0,2) eastbound.
  - Abort (0:5880 = 0) if the in-cell distance along the player's axis is >= 8000 (|dy| for odd q, |dx| for even q; cell difference ignored).
  - Clear collision states; target = clamp(2*player.cell - car3.cell, 1..38) (past the player); aux +0x0a = 100, +0x0c = 200; active; colour 1:2342 = rnd 1..7; aux +0x1a = 200 + 32*(colour-1).
- Colour: 0:e5d0(D0 = 1:2342, A0 = palette 0:a520) copies 4 RGB words from 0:a6f4 + 8*D0 into colours 4..7 of the second copper segment (A0+0x34). Index 0 is red (f66,f00,a00,600), shown when no traffic car is active; car2 and car3 share these colours. Only one other car is ever inserted into the scene (car2 has priority, 0:2452/0:2cb8).

## 10. Mission classes, arrest and shooting

### 10.1 Mission class (1:232a after init) and 0:4f14 CheckArrest
- If 1:235e (player touched car2): class 1: success (1:235a = -1) only if the player was stopped at the contact (1:22e0), i.e. a roadblock. Class 0/2: once player speed < 20: speed = 0, 1:235e = 0, penalty 0:cdc8(0x20).
- Else if car2 hit count 0:4f10 == 5: class 2 -> success; other classes -> penalty 0x20 every frame while the count stays 5 (it never changes again).
- Else if siren on, class 0 and same cell as car2: if car2's sprite position is on screen ([0,320)x[0,200)), car2 is less than 1000 units ahead in the player's travel frame (any lateral, negative also accepted) and |car2.h - player.h| < 180 (raw word difference, not wrapped): success.
So, as far as the code shows (labels unverified): class 0 (missions 1-2) = siren + close behind the suspect; class 1 (3-4) = stop and let the suspect hit you; class 2 (5-6) = shoot the suspect's car 5 times; the suspect shoots back only in class 2.

### 10.2 Player shooting, 0:4d82 + 0:4dca
- 'T' toggles gun mode 1:2336 (1:2338 = complement, used by the HUD). With gun mode on and fire held, every frame: shot sound (0:1394) and 0:1162 immediately; 0:4f0e (hit) = 0.
- Target = the car drawn this frame: bank 1:232e (-1 = none), LOD frame 1:232c (must be < 6), screen pos 1:2332/1:2334 (y already clamped to <= 138). Frame header via offset word at bank+2*frame: +2 width in words, +4 height, +6/+8 hotspot. Hit if the crosshair (1:233c, 1:233e) is inside the sprite rectangle; 0:4f0e = -1.
- car2: must also be inside the lower half of the rectangle; 0:4f10++; when it becomes exactly 5, car2 channel B is forced (+0x22 = -0x100, contact (100,0), +0x38 = -0x100): it spins out without damage/push.
- car3: 0:4f12++, penalty 0:cdc8(0x40), same forced spin.
- The crosshair is fixed ahead (x 160 +/- jitter*speed/400, y 80 + bounce/2), so aiming is done by steering.

### 10.3 Suspect shooting, 0:50b6 (every 6th frame, class 2, same cell as car2)
- Player position in car2's frame (rotate by 180 - car2 +0x10): if behind and < 4000 away, fire backwards (car2.h + 360); if ahead and < 4000, fire forwards.
- Bullet line from car2 (player frame) to a point 4000 units along the fire direction; 0:1eb2 clips it at depth 350; screen point `x = (X<<8)/350 + 160`, `y = (((car2 +0x48)-100)<<8)/350*2 + 70`.
- If inside [0,0x13f)x[0x10,0x84): shot sound; if 10 holes already: player killed (1:2358); else add a hole (x,y) to 0:53e4 (count 0:53e2); a hole inside [0x8c,0xb4)x[0x32,0x5a) also kills (1:2358). Holes are drawn every frame with BALLE.IMG and never removed during the mission.

### 10.4 BCD counter 1:236a (4 bytes packed BCD)
0:cd84 BcdAdd(D0.l) uses `abcd` byte by byte from fc15 to fc12. 0:cdc8 BcdSub(D0.l) uses `sbcd`, then clears the long if the low digit of the word at 1:236a is 9 (underflow detection; a value with that digit legitimately 9, i.e. >= 90000, would be cleared too). Uses: mission bonus (+2000/5000/10000), -1 every 6 frames, -20 per car contact (per vehicle) and per suspect touch, -40 per traffic car hit. 0 ends the mission (0x80). HUD prints 5 digits (0:e48c). Whether it is a score, a timer or both is unverified (it is decremented by time).

## 11. Rendering routines in this area

- 0:2452 BuildViewObjects: for each visible cell i (count 1:2baa, cells 1:2c2c {cellX, cellY, type}, offsets 1:2c44 = cellDelta*0x4000 - player.pos): copy the COOR_OBJ.BIN list (long offset table by cell type, list = word count (low byte; -1 = empty) + 10-byte entries {x,y,z,type,extra}) into buffer 1:2ecc + i*0xfa0 with translation. Insert one car: car2 if same cell (slot 0) or in the second visible cell (slot 1); else car3 via 0:2cb8. Inserted entry {x, y, z, 2, 0}, z = 0 for car2 (the computed 100 - +0x48 is overwritten, 0:253c), z = 100 - +0x48 for car3; translated position also stored in 1:2364/1:2366; 0:2d90 = 0 (car2) or -1 (car3). Then rotate all slots by 180 - player +0x18, sense obstacles (8.1), cull (0:23e2), project with camH = 1:2368 (0:239c, entry becomes {sx, sy, depth, type, extra}), bubble sort by depth descending.
- 0:26e8 DrawObjects: slots drawn far to near. LOD from depth: `l = max(0,depth-256)>>6; l < 4 ? l : (l>>1) < 4 ? (l>>1)+2 : (l>>2)+4`, frame = LOD table[l] (cars: 0:8192, frames 1..10). Types 3..6 via 0:1658 (clip y 0x84). Others via BlitBob after 0:2dbc (y <= 0x8a). Car view: bearing = Atan2 of the car's view-frame position - 180; rel = car +0x18 - player +0x18 - bearing; view = ((rel+15) mod 720)/30, >= 18 -> -24, +6 (24 views); bank per view from the type-2 table (VOITURE0..6); views 7..16 use mirrored frames (0:0ddc, flipped in place, per-frame flags 10 bytes per bank at 1:22e4 + 10*bank). Records 1:232c frame, 1:232e bank, 1:2332/1:2334 screen pos for shooting. Signs (8..11): q = ((player +0x18 - sign angle(+8 of entry) + 90) mod 720)/180, view = (q+1) mod 4 (0:2b50-0:2ba2, stored in 0:2dba); poles PAN_POT; type 11 with view 1/3 adds PST_STA and two poles offset by table 0:2d92 per LOD.
- 0:1658 DrawSmallBob(bank, frame, x, y, clipY, screen): CPU masked blit of 1- or 2-word wide, 4-plane frames (colour 0 transparent), hotspot subtracted, x clipped to -16/-32..320. Height clamp bug: if y+h-1 >= clipY the last row becomes 199, not clipY-1.
- 0:0ddc MirrorBobFrame(bank, frame, scratch): horizontal flip in place (bit-reverse table 0:0e6c, byte order reversed per row and plane), hotspot x := width*16 - hotspot x.
- 0:2dbc ClampSpriteY: D2 = min(D2, 0x8a).
- 0:e5d0 SetCarColours (see 9.5).

## 12. Per-function summary

| Address | Proposed name | Evidence |
|---|---|---|
| 0:4d82 | ToggleGunMode | KeyDown(0x54), latch 0:4dc8, `eori` 1:2336, 1:2338 = seq |
| 0:4dca | PlayerShoot | fireButton + 1:2336 gate, shot sound, sprite-rect test vs 1:233c/6, 0:4f10/f12 counters, forced channel B |
| 0:4f14 | CheckArrest | sets 1:235a (end bit 0x40) by class 1:232a; penalties via 0:cdc8 |
| 0:5064 | ToggleSiren | KeyDown(0x53), latch 0:50b2, 0:50b4 -> 0:1392 |
| 0:50b6 | SuspectShoot | every 6 frames (0:540c), class 2, bullet clip at depth 350, writes 0:53e4/0:53e2, 1:2358 |
| 0:540e | UpdateTrafficCar | spawns/retargets 1:3e0c/1:3f38, 0:5880 active flag, 1:2342 colour |
| 0:4a88 | CollidePlayerSuspect | box test in player frame, +0x2c triggers, 1:235e/1:22e0 |
| 0:4c10 | CollidePlayerTraffic | same with car3, 1:235c |
| 0:2452 | BuildViewObjects | COOR_OBJ copy/rotate/cull/project/sort; player sensing into +0x86 |
| 0:26e8 | DrawObjects | LOD, car view selection, BlitBob/0:1658 per type |
| 0:2bb2 | SenseSuspectObstacles | car2 own cell, box test into car2 +0x86 |
| 0:2cb8 | InsertTrafficCarObject | tail of 0:2452 (jumped to, returns via bra 0:256e) |
| 0:2dbc | ClampSpriteY | D2 <= 0x8a |
| 0:1658 | DrawSmallBob | CPU and/eor 4-plane blit |
| 0:0ddc | MirrorBobFrame | bit-reverse table 0:0e6c |
| 0:cd84 | AddScoreBCD | abcd into 1:236a |
| 0:cdc8 | SubScoreBCD | sbcd from 1:236a, underflow clear |
| 0:e5d0 | SetCarColours | 0:a6f4 + 8*D0 -> palette A0+0x34 |
| 0:22a4 | OutCode2D | 4-bit outcode against 0:22f2..f8 |
| 0:22fa | CopyTranslateObjects(src,dst,dx,dy) | 10-byte entries, x+=dx, y+=dy |
| 0:2342 | CopyRotateObjects(src,dst,angle) | RotateXY inline |
| 0:239c | ProjectObjects(list,camH) | 0:1d98 in place |
| 0:23e2 | CullObjects(list) | 300 <= y < 0x2134, -4000 <= x < 4000 |
| 0:1bd2 | PolarToXY | see section 2 |
| 0:1bfa | CosSin | tables 0:83a0/0:8940 |
| 0:1c1a | Atan2 | table 0:8ee0 |
| 0:1d70 | RotateXY | Q14 rotation |
| 0:1d98 | ProjectPoint | /depth, +160/+70 |
| 0:2dca | UpdateVehicles | per-frame order in section 3 |
| 0:3a2e (+0:3d0c) | DriveVehicle | throttle/steer/yaw/slip/fuel/gear/rpm/temp |
| 0:3e7e | MoveVehicle | AdvanceInCell + cell clamp + crossing code |
| 0:1b56 | AdvanceInCell | pos += polar(speed, heading) with cell carry |
| 0:4068 | UpdateImpacts | channels A-E + crash |
| 0:49f8 | ClassifyContacts | +0x86 list -> triggers |
| 0:340a | CheckStationZone | type 11/12 rectangles -> +0x50 |
| 0:334c / 0:33da | FindNearestStation / MarkStationRobbed | table 1:23e0 |
| 0:3f26 / 0:3f62 | TargetCarAI / TrafficCarAI | 0:5a44 + 0:5c56 -> +0x60..+0x64 |
| 0:5a44 | PlanCellRoute | exits, entry, target -> path pointer |
| 0:5c56 | FollowCellPath | lane band, speed target |
| 0:5e92 | SteerToward | desired steer vs +0x0c |
| 0:3fa8 / 0:403e | CheckOffRoad / OffRoadSkidSound | backScreen pixel probes |
| 0:1eb2 | ClipEdgeNearBisect | bisection |
| 0:4906 | FlashTimer | 1:2348/1:2344 |

## 13. Globals in this area

1:232a mission number -> class; 1:232c/1:232e/da/dc last drawn car (frame, bank, screen x, y); 1:2336 gun mode, 1:2338 its complement; 1:233c/1:233e crosshair; 1:2340 player entry side; 1:2342 traffic colour; 1:2344/1:2348 car-contact palette flash; 1:234e direction-toggle request; 1:2350 toggle latch; 1:2352 same cell as suspect; 1:2354 within one cell of suspect; 1:2356 end reason; 1:2358 player shot; 1:235a arrest success; 1:235c touched traffic (unused); 1:235e touched suspect; 1:2360 crash sound request; 1:2364/1:2366 inserted car translated position; 1:2368 camera height; 1:236a BCD counter; 1:23de waypoints remaining; 1:23e0 waypoint table; 1:22e0 player was stopped at contact; 1:22e2 first-frames counter; 0:2d90 inserted car is car3; 0:2dba sign view; 0:4f0e hit this frame; 0:4f10 suspect hits; 0:4f12 traffic hits; 0:5880 traffic active; 0:5882 near traffic; 0:5884 near suspect; 0:5886 AI brake request; 0:50b4 siren; 0:53e2/0:53e4 bullet holes (max 10); 0:540c 6-frame counter; 0:22f2..f8 OutCode box.

## 14. Open questions and quirks

- Real frame rate (simulation is frame-locked; FlipScreens waits for a vblank) is unverified; a port must pick a fixed tick.
- Whether +y = north must be confirmed against the minimap/road renderer (compass evidence says yes).
- Meaning of the 1:236a counter (score vs timer) and the exact role of mission classes (labels inferred from the rules only).
- Purpose of the double direction toggle during the first two frames of a new game.
- 0:540e despawning a visible traffic car near the suspect but braking it when off-screen looks inverted; unverified intent.
- Original bugs worth reproducing or fixing deliberately: Atan2 on exact diagonals and on (0,0); car2 retarget writes +0x12 twice (0:3100/0:3104); 0:33da writes past the table when the cell is not found; car-car spin side is the same for the other car (+0x76 = 0/+1); penalty every frame while the suspect hit count stays 5 outside class 2; scenery sensing box follows the view (+0x18) while reversing; RPM garbage for one frame when the gear lags; DrawSmallBob height clamp to 199.
- +0x64 (fire input) and aux +0x18 are written but never used by the analysed code; the AI's "fire" output (big steering error) has no effect found.
- 0:ce1c (BCD halve of 1:236a) has no caller in the callgraph; undisassembled bytes after 0:bd4c may call it (unverified).
