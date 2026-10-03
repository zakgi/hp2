# Decisions

| Date | Decision | Notes |
|---|---|---|
| 2026-10-02 | Full port of Highway Patrol II, reverse engineering the whole executable including the game engine. | The graphics and presentation are kept; the playability is reworked. |
| 2026-10-02 | Targets: desktop host and RP2350. | Assets in flash, no heap in the core. |
| 2026-10-02 | Reworked rather than preserved: handling and controls, game loop and pacing, difficulty and AI, frame rate and responsiveness. | The original behaviour is still documented (`vehicles.md`, `game.md`) as the reference for what exists. |
| 2026-10-02 | Reference binary: the Quartex NTSC crack (`assets/hp.adf`), the only build available. | Crack changes found so far: manual-lookup protection unreferenced, "QUARTEX 1990!" on the end screens. |
| 2026-10-02 | Language: C++23. | |
| 2026-10-02 | Display model: 8-bit pixels (palette indices) in a screen split into at most two viewports, each with a 256-colour palette; copper colour changes inside a viewport become separate palette entries. | Replaces a per-row palette design. |
| 2026-10-02 | Input is recorded as keystroke events (press and release); the engine keeps no key levels. | |
| 2026-10-02 | Python tools live in `scripts/` with the `hp2lib` package, run from the repo's `.venv` (`uv sync`). | |
| 2026-10-02 | Data inside `hp.prg`: tables that can be computed (sine, cosine, arctangent) are rewritten; any other data the port takes from the executable is vetted case by case. The host loader reads the executable unrelocated, by hunk and offset. | Read from the executable so far: the title palette list (`1:28e0`), PRESENT.DIF's play list (`1:2870`, 27 `{frame, delay}` steps). |
| 2026-10-02 | The opening presentation keeps the original's pacing, from cycle counts of its busy waits (`game.md`, "Opening presentation"); the disk waits become a fixed 2 s hold behind the logo. Space or Enter skip to the finished title. | Counted, not measured: no emulator run yet. |
