"""Flash partition table shared by the header generator and the asset packer.

Reads a picotool-format partitions.json (src/boards/<board>/partitions.json):
the "firmware" partition and the data partitions after it, checked to be
sector-aligned and disjoint. The packer checks its image against the
partition it targets; the header generator emits one region per partition.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from itertools import pairwise
from pathlib import Path

RP2350_XIP_BASE: int = 0x10000000
FLASH_SECTOR: int = 4096


def parse_size(value: str) -> int:
    """Parse a picotool size literal ("256K", "1M") to bytes."""
    stripped: str = value.strip()
    if stripped.endswith(("K", "k")):
        return int(stripped[:-1]) * 1024
    if stripped.endswith(("M", "m")):
        return int(stripped[:-1]) * 1048576
    raise ValueError(f"expected '<N>K' or '<N>M', got {value!r}")


@dataclass(frozen=True)
class Partition:
    name: str
    offset: int  # bytes from the start of flash
    size: int  # partition size in bytes

    @property
    def address(self) -> int:
        return RP2350_XIP_BASE + self.offset

    @property
    def end(self) -> int:
        return self.offset + self.size


def load_partitions(path: Path) -> list[Partition]:
    data: dict[str, list[dict[str, str]]] = json.loads(path.read_text())
    partitions: list[Partition] = []
    entry: dict[str, str]
    for entry in data["partitions"]:
        name: str = entry["name"]
        offset: int = parse_size(entry["start"])
        size: int = parse_size(entry["size"])
        if offset % FLASH_SECTOR or size % FLASH_SECTOR:
            raise ValueError(f"{name}: partitions must be {FLASH_SECTOR}-byte aligned")
        partitions.append(Partition(name=name, offset=offset, size=size))
    ordered: list[Partition] = sorted(partitions, key=lambda partition: partition.offset)
    previous: Partition
    current: Partition
    for previous, current in pairwise(ordered):
        if current.offset < previous.end:
            raise ValueError(f"{current.name} overlaps {previous.name}")
    return partitions


def find_partition(partitions: list[Partition], name: str) -> Partition:
    matches: list[Partition] = [partition for partition in partitions if partition.name == name]
    if len(matches) != 1:
        raise ValueError(f"expected one partition named {name}, found {len(matches)}")
    return matches[0]
