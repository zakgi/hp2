# Data file formats

All files are in `assets/hp2/Highway Patrol II/DISK2_2/`; the executable opens them as `DF0:DISK2_2/<NAME>`. Multi-byte values are big-endian. Decoders: `scripts/hp2lib/images.py` (pictures, bobs, animation, fonts, palettes), `scripts/extract_images.py` (PNG dump to `work/assets/png/`), `scripts/extract_sounds.py` (WAV dump to `work/assets/wav/`), `scripts/render_map.py` (map).

## Pictures: .CPV

Full-screen 320x200x16 pictures. Decoded by `DecodeCPV` (`0:0f6c`).

```
+0    word   0x1234
+2    word   palette[16]       Atari ST format 0x0RGB, guns 0-7
+34   byte-run stream:
        c < 0x80  : the next byte, repeated c times
        c >= 0x80 : (c & 0x7f) literal bytes follow
```

Output order: plane 0, byte column 0 rows 0-199, then column 1, ... column 39; then planes 1-3. The decoder stops when plane 3 is full (several files end 2-3 bytes into a run). In memory the result is `word 0, palette[16], 32000 bytes of planes`, written at screen - 0x22 so the planes land on the screen.

Palette conversion (`PaletteSTToAmiga`): each gun through `{0,2,4,6,8,a,c,f}`. Several screens ignore the file's palette and use one from the executable (table below).

Files: LOGO, PRESENT, BUREAU (office), STATION, PAGE_F1-F6 (mission events), HELP (unused, see protection).

## Bob banks: .IMG

Sprite banks drawn by `BlitBob`; no palette in the file.

```
+0                 word  n
+2                 word  offset[n]          image k (1-based) at offset[k-1] from the file start
image:   +0  word  flags                    bits 0-3 stored plane count, bits 8-11 target plane mask
         +2  word  width in words
         +4  word  height
         +6  word  origin x                 subtracted from x when BlitBob's hotspot arg is set
         +8  word  origin y
         +10       plane data: stored plane count x height rows x width*2 bytes, plane after plane
```

Stored plane i goes to the i-th set bit of the plane mask (lowest first); flags seen: 0x0f04 (4 planes) and 0x0703 (3 planes to planes 0-2: VOITURE*, GAME_SCO image 1). Masked draws use the OR of the stored planes as mask (colour 0 transparent) and clear the absent planes under it.

| File | Images | Content |
|---|---|---|
| VOITURE0-6 | 10 each | the cars at 10 sizes (VOITURE1 = red sports car); 3 planes, colours 0-7 |
| CACTUS, BUISSON (bush), CAILLOUX (rocks) | 10, 10, 16 | roadside scenery at decreasing sizes |
| PAN_POT, PAN_GAU, PAN_CRO, PAN_DRO, PAN_ARR, PAN_PRO | 10 each | road signs (PAN = panneau) |
| PST_PRO, PST_STA, PST_FLG, PST_FLD | 10 each | station objects (PST = poste) |
| DES_TABB | 5 | cockpit: 1 steering wheel rim, 2-3 hands, 4 dashboard (320x68), 5 roof strip (320x16) |
| DEC_FOND | 6 | horizon backdrop strips |
| BALLE | 3 | gun, bullet, muzzle flash (*unverified* order beyond 1 = gun, 3 = flash) |
| BUREAU | 10 | office: 1-6 wanted posters, 7-9 drawer fronts, 10 pointer |
| STATION | 8 | station menu items and attendants |
| GAME_SCO | 12 | 1 GAME OVER, 2 SCORE:, 3-12 digits 0-9 |
| NAME | 3 | title overlays; 1 (credits list) is never drawn |
| MODULE | 12 | map tile icons for the (removed) protection question |

## Animation: .DIF

XOR delta frames over an Atari ST low-res screen (16 pixels = 4 interleaved plane words, 160 bytes per row). Only PRESENT.DIF (19 frames), played by `PlayDIF` with the play list `presentPlayList` (`1:2870`: `{word frame, word delay}` until frame 0; 27 steps, frames 10 and 11 repeat to flash).

```
+0       long  n
+4       long  offset[n]           frame k at offset[k-1]
frame:   repeated { word count (0 = end), word byte offset, word xor[count] }
```

## Fonts: LETTRE1.BIN, LETTRE2.BIN

94 glyphs (ASCII 0x20-0x7d), 32 bytes each: 8 rows x {plane 0, 1, 2, 3 byte}. The two files differ in colours only.

## Map: CARTE.BIN

64x64 bytes, index `y*64 + x`, road cell type 0-12 per byte (shapes and counts in `renderer.md`). Cars are clamped to cells 0-39.

## Object placement: COOR_OBJ.BIN

11228 bytes, loaded at the start of each mission. Per road cell type, an object list in cell coordinates: a long offset table by cell type, then lists of `word count` (low byte; -1 = empty) + 10-byte entries `{x, y, z, type, extra}`. Types: 1 cactus (fences of the stations too), 3-6 stones, 7 bush, 8-10 road signs, 11 station sign (`extra` = sign angle for the 4 sign views); type 2 (car) is inserted at run time. The game takes the count's low byte as the entry count (`dbf` on count - 1, so a low byte of 0 would run 65536 times; no list has one); the port's decoder reads it as that many entries, 0 for none. Used by `BuildViewObjects` / `DrawObjects` (`renderer.md`, `vehicles.md` sections 8.1 and 11).

## Sounds: .SND

Raw signed 8-bit samples (MOTEUR.SND and DERAP.SND are IFF 8SVX files, but the game ignores the chunks and plays fixed byte ranges). All five are loaded into one 0xab1c-byte chip buffer (`sfxBuffer`).

| File | Played bytes | Period | Use |
|---|---|---|---|
| MOTEUR.SND | +0x92, 0x22a bytes, looped | 0x280 - player speed (640 idle .. 240 at speed 400) | engine, channel 0 |
| SIRENE.SND (Sirene.snd on disk) | whole, looped | 864 | siren, channel 1 |
| TIR.SND | whole | 427 | gunshot, channel 2 |
| DERAP.SND | whole including the 0x68-byte IFF header | 900 | skid, channel 3 (volume 0x20 when leaving the road) |
| CHOC.SND | whole | 640 | crash, channel 2 |

The 8SVX headers give rates that match the NTSC clock exactly (3579545 / 900 = 3977 Hz). MOTEUR.SND's header loops its whole 0x254-byte body at 6628 Hz; DERAP.SND's is a 0x1c1c-byte one-shot at 3977 Hz. SIRENE, TIR and CHOC have no header: their rates exist only as the periods in the code.

## Music: .MUS

Only HIGHWAY.MUS, the title music. 15 long instrument sizes in bytes (the player places the instruments with them), then a 15-sample Soundtracker module:

```
+0x00   20 bytes  title; the word at +4 is the tempo, in CIA timer counts per tick (HIGHWAY.MUS: "SONG1" 0x99 -> 0x3199)
+0x14   15 x 30   instruments: name[22], word length (words), word volume (low byte, 0..64),
                  word loop start (bytes), word loop length (words; 1 = no loop)
+0x1d6  byte      song length (positions)
+0x1d8  128 bytes positions: pattern numbers
+0x258            patterns, 1024 bytes each (as many as the highest position + 1):
                  64 rows x 4 channels x {word period, byte instrument << 4 | effect, byte parameter}
then              the instruments' bytes, back to back, sizes from the table before the module
```

Periods are Paula periods of the PAL clock (428 = C-2). Two special periods: `0xfffe` silences the channel, `0xfffd` clears its effect. An instrument plays `length` bytes when a note starts, then repeats its loop; "nappes" (instrument 3) is the only looping one, a 6306-byte head and the loop after it. HIGHWAY.MUS uses no effects and one `0xfffe`: 9 positions over 7 patterns, at about 56 ticks a second (6 ticks a row).

## Palettes in the executable

Copper palette lists (`{first, count, lines to next segment, rgb[count]}...`):

| Address | Format | Used for |
|---|---|---|
| `1:28e0` titlePalette | ST | title: sky ramp rows 0-35, PRESENT.CPV colours from row 36 |
| `0:ca94` officePalette, `0:ca6e` officePaletteDim | Amiga | office |
| `0:7766` scorePalette | Amiga | end and score screens, GAME_SCO.IMG |
| `0:a520` viewPalette | Amiga | driving screen (see `renderer.md`) |
| `0:a734` viewPaletteRed | Amiga | driving screen, red flash |
| `0:c338` protectionPalette | Amiga | removed protection screen, MODULE.IMG |

## Road shapes in the executable

`roadCellShapes` (`0:7812`) holds 13 long pointers, one per road cell type, to outlines that follow it in hunk 0 from `0:7846`. An outline is a word count n (only the low byte is used; 0xffff for none, as for type 0) and n + 1 points of three signed words {x, z, y} in cell units, the last equal to the first; y is 0 for every shape. `TranslatePolygon3D` (`0:1c98`) reads one outline. Each is followed by a 0xffff word the code does not read (an end-of-list marker, *unverified*). The port lists the 13 places (`kRoadShapePlaces` in `asset_manager.cpp`) instead of following the pointers. The shape of each type: `renderer.md`, "World".
