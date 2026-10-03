# Coding style

Highway Patrol II is a native, multiplatform C++23 port of the 1990 Amiga game (desktop host and RP2350). Readable code, documented behaviour and preservation of the original formats are project goals. Python tools extract and explain resources before the port uses them. Ghidra is the annotated map of the original program.

## Formatting and source organization

- C++23. Headers use `#pragma once` and include their own dependencies.
- Namespace `hp2`; host-only code in `hp2::host`.
- [`.clang-format`](../.clang-format) and [`.clangd`](../.clangd) govern formatting and diagnostics: Google-derived style, two-space indentation, 120 columns, unindented namespace bodies, pointer-left (`Widget*`). Run `clang-format` on changed C++ files; keep unrelated formatting out of a change.
- ASCII in source files, comments, identifiers and technical notation.
- Files focus on one format or subsystem. No generic utility layers until there is a concrete shared use.

## Names

| Item | Convention | Example |
| --- | --- | --- |
| Types and member/free functions | `PascalCase` | `HunkFile`, `DecodeCpv` |
| Variables, parameters, fields, files, namespaces | `snake_case` | `split_row`, `palette_list.cpp` |
| Compile-time constants and enumerators | `kCamelCase` | `kColorRegisterCount`, `Viewport::kLower` |
| Preprocessor macros | `UPPER_SNAKE_CASE` | only when required |

Descriptive names of at least three characters for variables, parameters and fields (`index`, `offset`, `count`); type names and enumerators are exempt. Include units when the type does not carry them: `delta_seconds`, `size_bytes`. Distinguish file offsets, hunk offsets and Ghidra addresses.

Synchronous operations use direct verbs: `Load`, `Decode`, `Read`, `Render`. `On...` is reserved for callbacks (and the component hooks `OnEnter`/`OnExit`).

## Declarations

- Declare initialized variables with `auto`: `auto count = std::size_t{0};`, `const auto hunks = HunkFile::FromBytes(...)`, `for (const auto& hunk : file.hunks)`. Spell the type inside the initializer, not before the name.
- Prefer braced initialization and designated initializers for aggregates.
- `std::optional<T>` when absence is the only failure information; `std::expected<T, Error>` when callers distinguish errors. No sentinel results, no error out-parameters. `[[nodiscard]]` where ignoring a result would lose validation.
- Prefer a single return at the end of a function: assign a `result` and return it. An entry guard is acceptable when it removes deep nesting.
- `and`, `or`, `not` for boolean logic. Brace every `if`, `else`, `for`, `while` and `do` body.

## Values, ownership and containers

- Values and references first; pointers only where an external API or a representation needs them, never as a stand-in for an optional.
- `std::array` for fixed storage, `std::span` for buffer views; no pointer-plus-length interfaces, no C arrays for storage.
- Engine storage is static where possible: owned fixed-size arrays or caller-provided buffers with documented capacities and overflow handling. No per-frame heap allocation in the core.
- `std::string_view` for borrowed text; document the lifetime behind every view.
- RAII for resources. No `new`/`delete`, `malloc`/`free`, C-style casts or macros standing in for typed operations.

## Original data

- The original is big-endian 68000 code. Read file and executable fields with the explicit big-endian helpers (`host/format/endian.hpp`) into native integers; do not overlay structs on file bytes.
- Fixed-width integer types for file fields and for arithmetic whose width affects behaviour; `std::size_t` for host buffer sizes. `enum class` with an explicit underlying type.
- Decode once, in the host loader, which owns the layout knowledge (offsets, plane order, compression). Engine-facing types present the data the way a modern engine uses it: one colour index per pixel instead of bitplanes, spans and plain integers instead of planar buffers and segment pointers. Do not carry the original's buffer sizes or memory tricks into engine types.
- Validate lengths, counts, offsets and arithmetic before access. Unknown fields stay explicitly unknown until evidence supports a meaning.
- Where original behaviour depends on 16-bit wrap-around, sign extension or discrete transitions, express it with defined C++ operations and cite the routine (`DecodeCpv`, `0:0f6c`).

## Display model

The original draws 4 bitplanes and changes colours down the screen with the copper. The port draws 8-bit pixels (palette indices) into a `Screen` that is one viewport, or two once split at a row; each viewport has its own 256-colour palette. A screen splits where its second full palette starts (the title picture, the dashboard); colour changes inside a viewport (the sky gradient, the ground haze) are separate entries of that viewport's palette. The host `Renderer` resolves pixels through the palettes; the core never sees RGBA.

## Errors and core boundaries

Core code uses neither exceptions nor RTTI, including headers the core includes. Host code may catch exceptions from dependencies (`args`, SFML) and translates failures at that boundary.

The core models the game and works on explicit state and asset views. Windowing, input devices, audio output, file access, command-line parsing and diagnostics belong to the host. Use `std::filesystem` and streams for host file handling.

## State and interfaces

State, configuration and resources live in the objects that own them; dependencies are passed explicitly. No ambient globals, file-static singletons or static class state (per-template `static constexpr` constants excepted). `const` honestly; no `mutable` to hide changes.

Prefer templates and concepts to inheritance when the implementation is chosen at compile time (`ComponentLike`). Lambdas only for small operations passed to algorithms.

Input is a queue of keystroke events (press and release). Components consume the events; nothing in the engine reads key levels.

## Python tools

Python scripts are readable reference implementations as well as utilities.

- Python conventions: `snake_case` functions and variables, `PascalCase` types, `UPPER_SNAKE_CASE` module constants. Full type annotations; ruff, black (120 columns) and mypy pass.
- `scripts/hp2lib` is the package; scripts import it directly (`.venv` from `uv sync`), no `sys.path` manipulation.
- Keep decoding separate from command-line parsing and file I/O. Model entities with dataclasses; parse in named static factories (`from_data`).
- Validate truncated input, out-of-range offsets and impossible counts; report the file and offset.
- Leave source assets unchanged; write converted output to an explicit directory (`work/`).

## Preservation documentation and Ghidra

Document each format and subsystem as it becomes understood, for a reader who has not followed the investigation: offsets, widths, byte order, signedness, counts, compression, examples, validation, unknowns; purpose, state, call relationships, algorithms, units and timing for subsystems, plus any intentional difference in the port.

Distinguish evidence: observed (binary data, disassembly, runtime trace), inferred (*unverified* in the documents), unknown. Places in `hp.prg` are written `hunk:offset`, the offset in hexadecimal (`1:2870`); Ghidra addresses appear only in the Ghidra tooling (`building.md` gives the mapping).

Keep Ghidra names, labels, types and comments consistent with the documents and the code. Code comments explain present behaviour and non-obvious reasons; investigation history and decisions belong in Markdown (`docs/decisions.md`).
