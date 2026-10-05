# Building

## C++ port

CMake 3.28 or newer, Ninja and a C++23 compiler. The first configure fetches the pinned SFML 3.1, args, spdlog and GoogleTest into `.cache/fetchcontent/`, shared by every build directory.

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host
```

Run `build/hp2` from the repository root, or pass the game disk: `build/hp2 --disk assets/hp2.adf`. The game files are read straight from the ADF (ADFlib); the extracted copy under `assets/hp2/` is for the Python tools. `--scaling 1`-`8` sets the window size (default 3). The program plays the opening presentation with its music (Space or Enter skip it), then the office, where a mission is chosen; Escape quits. Choosing a mission opens the highway, seen from the driver's seat; the arrows drive, P pauses, M shows the map with the player's car and the criminal's, Escape abandons the mission. `--start office`, `--start highway`, `--start station` (with `--robbed` for a robbed station) and `--start ending --ending <reason>` (out-of-fuel, stations-robbed, overheated, wrecked, tires-gone, shot, arrest, bounty-gone; `--score <n>` for the score) open the other screens directly.

`compile_commands.json` at the repository root is a symlink to the last configured build, for clangd.

### Layout

- `src/core/`: platform-independent engine code, built without exceptions or RTTI, for the host and the firmware alike: components and their engine, the action stack that times presentations, keystroke events, the split screen with its two 256-color palettes, `EngineAssets`, every asset of the game as views in tables indexed by enums (`engine_assets.hpp`), and audio: voices resampled with a 6-point B-spline, the music player, and the engine that mixes them at 48 kHz into a lock-free ring.
- `src/host/format/`: decoders for the original files: AmigaDOS hunk executables (read unrelocated, by hunk and offset), `.CPV` pictures, `.IMG` bob banks, `.DIF` delta animations (as pixel XOR masks) and their play list, `.MUS` music, IFF 8SVX sounds, the `LETTRE*.BIN` fonts, the `CARTE.BIN` road map, the `COOR_OBJ.BIN` object placement, copper palette lists, bitplane deinterleaving.
- `src/host/`: the disk image reader (`adf.hpp`), the asset manager (reads and decodes the game files once, owns the storage behind the engine's views), the SFML renderer, keyboard input and the SFML audio stream that drains the engine's ring.
- `src/target/flash/`: the flash asset image's contract (record layouts, `asset_image.hpp`), its generated layout (`asset_layout.hpp`) and the boot check (`asset_check.hpp`).
- `src/target/rp2350/`, `src/boards/<board>/`: the firmware's board interface, cycle counter, hardware SHA-256, the SPI panel driver with its palette-lookup PIO program (`display/`), the presenter that sends the screen to the panel, keys over USB CDC, and per board its pins and clocks, the panel's init sequence, partition table and `main.cpp`.
- `cmake/`: the core source list, and the rp2350 toolchain bring-up, pico-sdk fetch, embedded flags and firmware target (with the PIO header generation).
- `test/`: GoogleTest suite. Tests that need the game files read the disk image `HP2_DISK_IMAGE` (default `assets/hp2.adf`) and skip when it is absent; decoded data is checked against digests from the Python reference decoders (`scripts/reference_digests.py` prints them).

## RP2350 firmware

```sh
cmake --preset rp2350-adafruit-feather
cmake --build --preset rp2350-adafruit-feather
```

Prerequisites: `arm-none-eabi-gcc`, `picotool` and Ninja on the path, and the `.venv` (the partition header is generated with it). The first configure fetches pico-sdk 2.3.1 with its tinyusb submodule into `.cache/fetchcontent/`. The output is `build-rp2350-adafruit-feather/hp2_rp2350.uf2`, which carries the partition table from the board's `partitions.json`. The presets set `PICO_COPY_TO_RAM`: the image runs from SRAM, XIP execution being slow on the RP2350. The core sources compile into the firmware too, so the cross-compiler reports what is not portable.

Current state: the board boots at 300 MHz, brings up the 480x320 SPI panel, waits up to ten seconds for a USB-CDC terminal, fills the panel blue through blocking SPI, starts the palette-lookup path and checks the asset image. When it verifies, `EngineAssets` is bound to it and the game runs from the title, 320x200 in the middle of the panel; without an image a test pattern of 16 color bars scrolls instead. There is no audio backend yet. The panel path and the engine loop have not been run on a board yet (*unverified*). Expected output with an image:

```text
[hp2] built <date> <time>
[board] sys_clk = 300000000 Hz, cycle counter available
[assets] PASS: 1542850 bytes at 0x10100000, verified in <n> cycles
[assets] 10 pictures, 27 sprite banks, title music of 1792 notes
[budget] frame 120: period <n> cycles, step <n> avg <n> worst, 0 dropped, component 0
```

The screen holds color indices; the panel takes RGB565. A PIO state machine turns each index into the address of its entry in a 256-entry table and a chained DMA pair feeds the entries to the SPI, so the CPU only writes the table (`src/target/rp2350/display/st7789v.hpp`, `palette_lut.pio`). A split screen goes out as one push per viewport, the table rewritten from the viewport's palette in between (`panel_presenter.hpp`).

Keys come from the serial terminal (`src/target/rp2350/cdc_input.hpp`): letters and digits are their own keys, Enter and Space too, the terminal's arrow sequences are the arrows and a lone ESC is Escape. A terminal sends no releases, so each byte is a press released at the next frame: a held key arrives as the terminal's repeats.

## Asset image

The game files never go into the firmware. The host build packs the decoded assets for the `assets` partition of the board's `partitions.json`:

```sh
cmake --build --preset host --target hp2_assets
```

runs `scripts/pack_assets.py` (reading the extracted game under `assets/hp2/`), which writes `build/assets/assets.bin`, `build/assets/assets.uf2` addressed at the partition's XIP base, and the checked-in `src/target/flash/asset_layout.hpp`: the image size and SHA-256, `AssetImage`, the image as a structure with one member per asset, and `FlashAssets(image)`, the engine's `EngineAssets` over it. The firmware hashes the partition with the RP2350 accelerator and, only on a match, calls `FlashAssets` on the image at the partition's address; erased flash reports the image absent. `test/host/asset_image_test.cpp` calls `FlashAssets` over `assets.bin` and compares every field with what the C++ asset manager decodes from the disk, so the Python packer and the C++ decoders cannot drift apart unnoticed.

Flash the image, then the firmware: a data download leaves the board in BOOTSEL, a firmware download reboots it.

```sh
picotool load -f build/assets/assets.uf2
picotool load -f build-rp2350-adafruit-feather/hp2_rp2350.uf2
picotool reboot
```

## Python tools

```sh
uv sync --extra dev
.venv/bin/python scripts/extract_images.py
```

`scripts/hp2lib` is the shared package (hunk loader, picture, palette, sound, music and world decoders, the flash partition table and UF2 packer, Ghidra client). Before considering a change done: `.venv/bin/ruff check scripts`, `.venv/bin/black --check scripts`, `.venv/bin/mypy --cache-dir work/mypy-cache scripts`.

| Script | Output |
|---|---|
| `extract_images.py` | every picture, bob bank, animation step and font as PNG in `work/assets/png/` |
| `extract_sounds.py` | the sound effects as WAV in `work/assets/wav/` |
| `render_map.py` | the CARTE.BIN road map as `work/assets/map.png` |
| `reference_digests.py` | the digests the C++ tests compare decoded data against |
| `pack_assets.py` | the flash asset image and its layout header (see "Asset image") |
| `gen_partition_header.py` | a board's `flash_partitions.hpp` from its `partitions.json` (run by the firmware build) |
| `callgraph.py` | per function of the Ghidra listing: place, size, callers, callees |
| `annotate.py` | applies a JSON annotation batch to the Ghidra project |
| `ghidra_script.py` | runs a Java GhidraScript (e.g. `ghidra_scripts/DumpListing.java`) against hp.prg |

Places in `hp.prg` are written `hunk:offset` everywhere (`1:2870`: hunk 1, offset 0x2870). Only the Ghidra tooling deals in Ghidra addresses, where hunk 0 starts at `0021f000` and hunk 1 at `0022d8a8` (`hp2lib.exe.to_ghidra`, `from_ghidra`); annotation batches take `hunk:offset` too.
