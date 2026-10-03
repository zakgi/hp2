# System layer

Evidence: Ghidra `hp2` / `hp.prg`; routine names and plates are in the Ghidra project. Hunk 1 (`1:0000`-) starts with these routines, followed by game variables and data.

## Executable

- `hp.prg`: 2 hunks, both typed CODE. Hunk 0 = 0xe8a8 bytes (game code and tables), hunk 1 = 0x3f9c bytes (system routines from `1:001c` to `1:2294`, then variables, filename records, data). No packer.
- `start` jumps to `main` (`0:a908`). `main` never returns: init, title, loading, then the game loop.
- `scripts/hp2lib/exe.py` loads the hunks with relocations applied (verified byte-identical to Ghidra's memory) and reads them by `HunkOffset`.

## Startup (`InitSystem`, `InitDisplay`)

- `Forbid()`, then the game takes the machine without disabling the OS: interrupt vectors $68 (level 2, keyboard) and $6c (level 3, vblank) are hooked and chain to the previous handlers.
- dos.library and trackdisk.device (unit 0) are opened; Workbench is closed via intuition (`CloseWindow(ActiveWindow)`, `CloseWorkBench()`).
- Display: two 32000-byte chip screens, an 8400-byte chip `scratchBuffer`, and a 0x34-byte copper list setting BPL1-4PT. 320x200 lowres, 4 planes, DIW 2c81-f4c1, DDF 38-d0, BPLCON0 0x4200. Colours 16-31 cleared.
- Each screen buffer is allocated 0x22 bytes larger and the pointer advanced by 0x22: picture decoders write a 34-byte header (word + 16 palette words) just before the bitmap.

## Display

- `FlipScreens`: waits for the blitter and the next vblank (`vblankCounter`), swaps `frontScreen`/`backScreen`, rewrites the copper's bitplane pointers.
- Palettes: `InstallPalette(list)` builds a new copper list: the 8 BPLxPT moves plus, per palette segment, a WAIT (from line 0x2c plus the accumulated `lines` values) and COLORxx moves. List format: repeated `{word first colour, word count, word lines to the next segment (0 = last), word rgb[count]}`. `SetPaletteColors` rewrites the colours in place (fades), `RemovePalette` restores the previous list.
- `ShowSystemDisplay` / `ShowGameDisplay` exist but nothing calls them.

## Blitter routines

- `BlitBob` (stack args, documented in its Ghidra plate): images from bob banks (`.IMG` files): word count, word offsets, then per image `{flags, width words, height, origin x, origin y, plane data}`. The mask is the OR of the stored planes (colour 0 transparent); flags bits 8-11 choose which of the 4 screen planes receive the stored planes, the others are cleared under the mask. Optional clipping, 16-pixel alignment, origin (hotspot).
- `DrawLine` (x0, y0, x1, y1, colour, screen): blitter lines per plane, clipped to 0..319 x 0..199 by bisection.
- `ClearScreen`, `CopyScreen` (32000 bytes, CPU movem).

## Input

- `ReadInput` (called once per frame): `joyX` (+1 right, -1 left), `joyY` (+1 up, -1 down), `fireButton` (-1 down) from joystick port 1 or cursor keys and space; mouse deltas halved into `mouseX`/`mouseY`, left button in `mouseButton`.
- `KeyDown(code)`: ASCII-like code mapped through `asciiToRawKey` (AZERTY layout; '0'-'9' are the keypad digits; 8-11 cursor keys; 0x0d return; 0x1b escape; 0x88-0x91 F1-F10) and tested against `keyState`, which the level 2 handler fills.

## Disk

- `LoadFile(name, buffer, size)`: buffer -1 allocates chip memory, size -1 takes the size from `Examine`. Returns D0 = size, A0 = buffer, D1 = error.
- Before each load it reads the root block of DF0 through trackdisk and checks the volume name starts with "Highway Patrol"; otherwise it shows an INSERT DISK picture (`loadDiskPrompt`, 16 bytes x 29 rows) in the screen centre, waits for space and retries. The motor is switched off after the load.
- Filename records in hunk 1: the string, then a long buffer pointer and a long size.

## Music

Soundtracker-style player for HIGHWAY.MUS (file = 0x3c-byte header + a 15-sample module: sample headers at +0x14, song length at +0x1d6, positions at +0x1d8, patterns at +0x258). Timer: CIA-A timer A via `ciaa.resource` AddICRVector; the interrupt stub at `1:1d06` calls `MusicTick`, which plays a row every 6th tick (`MusicPlayRow`, `MusicChannelNote`) and runs effects otherwise (`MusicChannelEffects`: arpeggio, slides, filter on/off, tone portamento, volume slides 5/6). Period table `musicPeriodTable`. `MusicStart`/`MusicStop` are called only around the title sequence.

Sound effects use a separate mechanism (`0:1162`), described in the events/sound notes.
