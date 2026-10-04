"""Decoders for the road map (CARTE.BIN), the scenery placement (COOR_OBJ.BIN) and the road shapes in hp.prg.

They mirror src/host/format/road_map.cpp, object_placement.cpp and road_shapes.cpp; the formats are
described in docs/formats.md ("Map: CARTE.BIN", "Object placement: COOR_OBJ.BIN", "Road shapes in
the executable").
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAP_SIZE = 64
CELL_TYPE_COUNT = 13
NO_OBJECTS = 0xFFFF
OBJECT_ENTRY = struct.Struct(">hhhHH")


class FormatError(ValueError):
    """Raised when a file does not match the layout its decoder expects."""


def decode_road_map(data: bytes) -> bytes:
    """The 64 x 64 road cell types, row after row."""
    if len(data) != MAP_SIZE * MAP_SIZE:
        raise FormatError(f"road map of {len(data)} bytes, expected {MAP_SIZE * MAP_SIZE}")
    if max(data) >= CELL_TYPE_COUNT:
        raise FormatError(f"road cell type {max(data)}")
    return bytes(data)


@dataclass(frozen=True)
class PlacedObject:
    x: int
    y: int
    z: int
    type: int
    extra: int


def decode_object_placement(data: bytes) -> list[list[PlacedObject]]:
    """Every road cell type's object list: a long offset per type, each to a word count (low byte;
    0xffff for none) and 10-byte entries {x, y, z, type, extra}."""
    if len(data) < 4 * CELL_TYPE_COUNT:
        raise FormatError("object placement table is truncated")
    lists: list[list[PlacedObject]] = []
    for offset in struct.unpack_from(f">{CELL_TYPE_COUNT}I", data, 0):
        if offset + 2 > len(data):
            raise FormatError(f"object list at {offset:#x} is outside the file")
        count_word = struct.unpack_from(">H", data, offset)[0]
        count = 0 if count_word == NO_OBJECTS else count_word & 0xFF
        if offset + 2 + count * OBJECT_ENTRY.size > len(data):
            raise FormatError(f"object list at {offset:#x} runs past the end")
        lists.append(
            [
                PlacedObject(*OBJECT_ENTRY.unpack_from(data, offset + 2 + index * OBJECT_ENTRY.size))
                for index in range(count)
            ]
        )
    return lists


NO_OUTLINE = 0xFFFF
OUTLINE_POINT = struct.Struct(">hhh")


@dataclass(frozen=True)
class ShapePoint:
    x: int
    y: int


def decode_road_shape(data: bytes) -> list[ShapePoint]:
    """One road outline from the start of ``data``, as TranslatePolygon3D (0:1c98) reads it: a word
    count n (low byte; 0xffff for none), then n + 1 points of words {x, z, y}, the last equal to
    the first. x and z are kept, z running along the map's y; y must be 0."""
    if len(data) < 2:
        raise FormatError("road outline is truncated")
    count_word = struct.unpack_from(">H", data, 0)[0]
    if count_word == NO_OUTLINE:
        return []
    count = (count_word & 0xFF) + 1
    if len(data) < 2 + count * OUTLINE_POINT.size:
        raise FormatError(f"road outline of {count} points is truncated")
    points: list[ShapePoint] = []
    for index in range(count):
        east, north, height = OUTLINE_POINT.unpack_from(data, 2 + index * OUTLINE_POINT.size)
        if height != 0:
            raise FormatError(f"road outline point {index} is at height {height}")
        points.append(ShapePoint(east, north))
    if points[0] != points[-1]:
        raise FormatError("road outline is not closed")
    return points
