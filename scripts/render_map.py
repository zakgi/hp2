"""Render CARTE.BIN with the road cell shapes from hp.prg to a PNG.

usage: python3 scripts/render_map.py [--scale 24] [--out work/assets/map.png]

Each map byte (64x64, row-major, y*64+x) selects a road polygon from roadCellShapes
(0:7812). Cell size is 0x4000 world units; the image puts cell (x, y) at column x,
row y, with world z growing downward, as the shape coordinates are laid out.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw

from hp2lib.exe import DATA_DIR, REPO, Executable, HunkOffset

ROAD_CELL_SHAPES = HunkOffset(0, 0x7812)
CELL_TYPES = 13
CELL_UNITS = 0x4000
STATION_TYPES = (11, 12)


def cell_shapes(exe: Executable) -> list[list[tuple[int, int]]]:
    shapes: list[list[tuple[int, int]]] = []
    for pointer in exe.pointers(ROAD_CELL_SHAPES, CELL_TYPES):
        count = exe.word(pointer, signed=True)
        if count == -1:
            shapes.append([])
            continue
        n = (count & 0xFF) + 1
        points = exe.words(pointer + 2, n * 3)
        shapes.append([(points[3 * i], points[3 * i + 1]) for i in range(n)])
    return shapes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scale", type=int, default=24, help="pixels per cell")
    parser.add_argument("--out", type=Path, default=REPO / "work" / "assets" / "map.png")
    ns = parser.parse_args()

    exe = Executable()
    shapes = cell_shapes(exe)
    grid = (DATA_DIR / "CARTE.BIN").read_bytes()
    s = ns.scale
    image = Image.new("RGB", (64 * s, 64 * s), (196, 160, 104))
    draw = ImageDraw.Draw(image)
    for y in range(64):
        for x in range(64):
            kind = grid[y * 64 + x]
            if kind >= CELL_TYPES or not shapes[kind]:
                continue
            colour = (200, 40, 40) if kind in STATION_TYPES else (60, 60, 60)
            points = [(x * s + px * s / CELL_UNITS, y * s + pz * s / CELL_UNITS) for px, pz in shapes[kind]]
            draw.polygon(points, fill=colour)
    for i in range(0, 65, 8):
        draw.line([(i * s, 0), (i * s, 64 * s)], fill=(150, 120, 80))
        draw.line([(0, i * s), (64 * s, i * s)], fill=(150, 120, 80))
    ns.out.parent.mkdir(parents=True, exist_ok=True)
    image.save(ns.out)
    print(f"wrote {ns.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
