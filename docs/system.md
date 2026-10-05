# System layer

Evidence: Ghidra `hp2` / `hp.prg`; routine names and plates are in the Ghidra project. Hunk 1 (`1:0000`-) starts with these routines, followed by game variables and data.

## Executable

- `hp.prg`: 2 hunks, both typed CODE. Hunk 0 = 0xe8a8 bytes (game code and tables), hunk 1 = 0x3f9c bytes (system routines from `1:001c` to `1:2294`, then variables, filename records, data). No packer.
- `start` jumps to `main` (`0:a908`). `main` never returns: init, title, loading, then the game loop.
- `scripts/hp2lib/exe.py` loads the hunks with relocations applied (verified byte-identical to Ghidra's memory) and reads them by `HunkOffset`.

## Startup (`InitSystem`, `InitDisplay`)

- `Forbid()`, then the game takes the machine without disabling the OS: interrupt vectors $68 (level 2, keyboard) and $6c (level 3, vblank) are hooked and chain to the previous handlers.
- dos.library and trackdisk.device (unit 0) are opened; Workbench is closed via intuition (`CloseWindow(ActiveWindow)`, `CloseWorkBench()`).
- Display: two 32000-byte chip screens, an 8400-byte chip `scratchBuffer`, and a 0x34-byte copper list setting BPL1-4PT. 320x200 lowres, 4 planes, DIW 2c81-f4c1, DDF 38-d0, BPLCON0 0x4200. Colors 16-31 cleared.
- Each screen buffer is allocated 0x22 bytes larger and the pointer advanced by 0x22: picture decoders write a 34-byte header (word + 16 palette words) just before the bitmap.

## Display

- `FlipScreens`: waits for the blitter and the next vblank (`vblankCounter`), swaps `frontScreen`/`backScreen`, rewrites the copper's bitplane pointers.
- Palettes: `InstallPalette(list)` builds a new copper list: the 8 BPLxPT moves plus, per palette segment, a WAIT (from line 0x2c plus the accumulated `lines` values) and COLORxx moves. List format: repeated `{word first color, word count, word lines to the next segment (0 = last), word rgb[count]}`. `SetPaletteColors` rewrites the colors in place (fades), `RemovePalette` restores the previous list.
- `ShowSystemDisplay` / `ShowGameDisplay` exist but nothing calls them.

## Blitter routines

- `BlitBob` (stack args, documented in its Ghidra plate): images from bob banks (`.IMG` files): word count, word offsets, then per image `{flags, width words, height, origin x, origin y, plane data}`. Each call chooses masked or opaque. Masked: the mask is the OR of the stored planes (color 0 transparent), and the screen planes flags bits 8-11 leave out are cleared under it. Opaque: the whole rectangle is replaced. Flags bits 8-11 choose which of the 4 screen planes receive the stored planes. Optional clipping, 16-pixel alignment, origin (hotspot).
- `DrawLine` (x0, y0, x1, y1, color, screen): blitter lines per plane, clipped to 0..319 x 0..199 by bisection.
- `ClearScreen`, `CopyScreen` (32000 bytes, CPU movem).

## Input

- `ReadInput` (called once per frame): `joyX` (+1 right, -1 left), `joyY` (+1 up, -1 down), `fireButton` (-1 down) from joystick port 1 or cursor keys and space; mouse deltas halved into `mouseX`/`mouseY`, left button in `mouseButton`.
- `KeyDown(code)`: ASCII-like code mapped through `asciiToRawKey` (AZERTY layout; '0'-'9' are the keypad digits; 8-11 cursor keys; 0x0d return; 0x1b escape; 0x88-0x91 F1-F10) and tested against `keyState`, which the level 2 handler fills.

## Disk

- `LoadFile(name, buffer, size)`: buffer -1 allocates chip memory, size -1 takes the size from `Examine`. Returns D0 = size, A0 = buffer, D1 = error.
- Before each load it reads the root block of DF0 through trackdisk and checks the volume name starts with "Highway Patrol"; otherwise it shows an INSERT DISK picture (`loadDiskPrompt`, 16 bytes x 29 rows) in the screen center, waits for space and retries. The motor is switched off after the load.
- Filename records in hunk 1: the string, then a long buffer pointer and a long size.

## Music

An Ultimate-Soundtracker-style replay routine for HIGHWAY.MUS (`formats.md`, "Music: .MUS"), started by `main` before the title fades in and stopped after `WaitFire`.

- `MusicStart` (`1:1bae`): song length and tempo from the module, instrument addresses from the size table, clears the first longword of every instrument (`MusicClearSamples`, so a one-word loop at the start repeats silence), adds a CIA-A timer A interrupt through `ciaa.resource` (`AddICRVector`) and loads the timer with the tempo word (`MusicStartTimer`). `MusicStop` removes the interrupt and silences the channels.
- `MusicTick` (`1:1d0e`, through the stub at `1:1d06`): counts ticks 1..6; on the 6th, `MusicPlayRow` (`1:1ed6`) plays a row, otherwise `MusicChannelEffects` (`1:1d64`) runs per channel. The first row plays on the sixth tick.
- `MusicChannelNote` (`1:1fa8`), per channel and row: an instrument number loads the instrument and writes its volume, adjusted by effect 5 (up by the parameter, at most 64) or 6 (down, at least 0); a period stops the channel's DMA, writes pointer, length and period (`0xfffe`: volume 0 instead) and marks the channel. `MusicPlayRow` then restarts DMA on the marked channels and, after a delay loop, writes every channel's loop pointer and length, so Paula plays the head once and the loop after it.
- `MusicChannelEffects`: a running slide (effects 7, 8) moves its period by its speed toward its target and stops there; otherwise by effect: 1 arpeggio (ticks 1..5: +high nibble, +low nibble, the note, +low, +high semitones, through `musicPeriodTable`, `1:21b6`: 36 periods from 856 to 113, 113 repeated, -1); 2 pitch bend (the high nibble added to the period each tick, else the low nibble subtracted); 3 and 4 the LED filter off and on (CIA-A port A bit 1); 7 and 8 set a slide toward the period `high nibble` semitones down or up, `low nibble` per tick.

The port's player (`src/core/music_player.hpp`) keeps the tempo, the rows and the effects' meaning, and leaves the hardware out: PAL clocks for pitch and tempo, the first row at once, equal-tempered semitones instead of the period table, a release instead of the volume-0 restart, no LED filter.

Sound effects use a separate mechanism (`0:1162`), described in the events/sound notes.
