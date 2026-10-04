"""Print the SHA-256 digests the C++ tests compare against, from the reference decoders.

Every digest is computed with hp2lib.images, independently of the port's decoders:

  logo rgb        LOGO.CPV under its header palette, RGB rows
  name N pixels   NAME.IMG image N (1-based), colour indices row-major
  animated pixels PRESENT.CPV after every step of PRESENT.DIF's play list (1:2870)
  font N pixels   LETTRE<N>.BIN, colour indices glyph after glyph (8x8 each, row-major)
  finished rgb    the finished title: animated picture, NAME.IMG images 2 and 3, the copied 8x8
                  cell (main 0:ad06-0:add0), under the title palette list (1:28e0), RGB rows

Atari ST colour words are decoded as the port decodes them (images.st_to_rgb), not through the
original's ST-to-Amiga table.
"""

from __future__ import annotations

import argparse
import hashlib
from collections.abc import Sequence
from pathlib import Path

import numpy as np

from hp2lib import images
from hp2lib.exe import DATA_DIR, DEFAULT_EXE, Executable, HunkOffset

TITLE_PALETTE = HunkOffset(1, 0x28E0)
PLAY_LIST = HunkOffset(1, 0x2870)
LOWER_NAME = (2, 10, 189)  # (image, x, y)
RIGHT_NAME = (3, 258, 143)
CELL_FROM = (240, 192)
CELL_TO = (160, 192)
CELL_SIZE = 8


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rgb_bytes(indices: np.ndarray, rows: Sequence[Sequence[tuple[int, int, int]]]) -> bytes:
    """RGB bytes of `indices`, colour index i on row r taking rows[r][i]."""
    table = np.array(rows, dtype=np.uint8)
    return table[np.arange(indices.shape[0])[:, None], indices].tobytes()


def st_rows(rows: Sequence[Sequence[int]]) -> list[list[tuple[int, int, int]]]:
    """Rows of ST colour words decoded as the port decodes them (images.st_to_rgb)."""
    return [[images.st_to_rgb(word) for word in row] for row in rows]


def animated_title(data_dir: Path, exe: Executable) -> np.ndarray:
    present = images.decode_cpv((data_dir / "PRESENT.CPV").read_bytes())
    frames = images.parse_dif((data_dir / "PRESENT.DIF").read_bytes())
    sequence = images.parse_dif_sequence(exe.read(PLAY_LIST, 0x100))
    planes = present.planes
    for _, _, _, planes in images.play_dif(present.planes, frames, sequence):
        pass
    return images.screen_to_indices(planes)


def finished_title(data_dir: Path, indices: np.ndarray) -> np.ndarray:
    screen = indices.copy()
    bobs = images.parse_bob_bank((data_dir / "NAME.IMG").read_bytes())
    for image, x, y in (LOWER_NAME, RIGHT_NAME):
        images.blit_bob(screen, bobs[image - 1], x, y)
    (fx, fy), (tx, ty) = CELL_FROM, CELL_TO
    screen[ty : ty + CELL_SIZE, tx : tx + CELL_SIZE] = screen[fy : fy + CELL_SIZE, fx : fx + CELL_SIZE]
    return screen


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--data", type=Path, default=DATA_DIR)
    ns = parser.parse_args()
    exe = Executable(ns.exe)

    logo = images.decode_cpv((ns.data / "LOGO.CPV").read_bytes())
    logo_rows = st_rows([logo.palette_st] * images.SCREEN_HEIGHT)
    print("logo rgb", digest(rgb_bytes(logo.indices(), logo_rows)))

    for bob in images.parse_bob_bank((ns.data / "NAME.IMG").read_bytes()):
        print(f"name {bob.index} pixels {bob.width}x{bob.height}", digest(bob.indices().tobytes()))

    for number in (1, 2):
        glyphs = images.decode_font((ns.data / f"LETTRE{number}.BIN").read_bytes())
        print(f"font {number} pixels", digest(b"".join(glyph.tobytes() for glyph in glyphs)))

    animated = animated_title(ns.data, exe)
    print("animated pixels", digest(animated.tobytes()))

    title_rows = st_rows(images.row_palettes(images.parse_palette_list(exe.read(TITLE_PALETTE, 0x100))))
    print("finished rgb", digest(rgb_bytes(finished_title(ns.data, animated), title_rows)))


if __name__ == "__main__":
    main()
