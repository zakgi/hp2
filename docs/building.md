# Building

## C++ port

CMake 3.28 or newer, Ninja and a C++23 compiler. The first configure fetches the pinned SFML 3.1, args, spdlog and GoogleTest into `.cache/fetchcontent/`, shared by every build directory.

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host
```

Run `build/hp2` from the repository root, or pass the game directory: `build/hp2 --game "assets/hp2/Highway Patrol II"` (the directory holding `hp.prg` and `DISK2_2/`). `--scaling 1`-`8` sets the window size (default 3). The program currently shows the title picture; Space, Enter or Escape quits.

`compile_commands.json` at the repository root is a symlink to the last configured build, for clangd.

### Layout

- `src/core/`: platform-independent engine code, built without exceptions or RTTI: components and their engine, the action stack that times presentations, keystroke events, the split screen with its two 256-colour palettes, the asset views the components draw from.
- `src/host/format/`: decoders for the original files: AmigaDOS hunk executables (read unrelocated, by hunk and offset), `.CPV` pictures, `.IMG` bob banks, `.DIF` delta animations (as pixel XOR masks) and their play list, copper palette lists, bitplane deinterleaving.
- `src/host/`: the asset manager (reads and decodes the game files once, owns the storage behind the engine's views), the SFML renderer and keyboard input.
- `test/`: GoogleTest suite. Tests that need the game files read `HP2_ASSET_DIR` (default `assets/hp2/Highway Patrol II`) and skip when it is absent; decoded data is checked against digests from the Python reference decoders (`scripts/reference_digests.py` prints them).

## Python tools

```sh
uv sync --extra dev
.venv/bin/python scripts/extract_images.py
```

`scripts/hp2lib` is the shared package (hunk loader, picture and palette decoders, Ghidra client). Before considering a change done: `.venv/bin/ruff check scripts`, `.venv/bin/black --check scripts`, `.venv/bin/mypy --cache-dir work/mypy-cache scripts`.

| Script | Output |
|---|---|
| `extract_images.py` | every picture, bob bank, animation step and font as PNG in `work/assets/png/` |
| `extract_sounds.py` | the sound effects as WAV in `work/assets/wav/` |
| `render_map.py` | the CARTE.BIN road map as `work/assets/map.png` |
| `reference_digests.py` | the digests the C++ tests compare decoded data against |
| `callgraph.py` | per function of the Ghidra listing: place, size, callers, callees |
| `annotate.py` | applies a JSON annotation batch to the Ghidra project |
| `ghidra_script.py` | runs a Java GhidraScript (e.g. `ghidra_scripts/DumpListing.java`) against hp.prg |

Places in `hp.prg` are written `hunk:offset` everywhere (`1:2870`: hunk 1, offset 0x2870). Only the Ghidra tooling deals in Ghidra addresses, where hunk 0 starts at `0021f000` and hunk 1 at `0022d8a8` (`hp2lib.exe.to_ghidra`, `from_ghidra`); annotation batches take `hunk:offset` too.
