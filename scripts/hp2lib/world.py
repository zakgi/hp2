"""Decoders for the road map (CARTE.BIN) and the scenery placement (COOR_OBJ.BIN).

They mirror src/host/format/road_map.cpp and object_placement.cpp; the formats are described in
docs/formats.md ("Map: CARTE.BIN", "Object placement: COOR_OBJ.BIN").
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
