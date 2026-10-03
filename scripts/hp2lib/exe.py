"""Load hp.prg (AmigaDOS hunk executable) with relocations applied, addressed by hunk and offset.

Tools read tables straight from the executable through this module, so they do not need
Ghidra running. Places are HunkOffset values, written ``hunk:offset`` (``1:2870``). The hunks
are loaded back to back where the Ghidra project has them, which is what the Ghidra tooling
converts through (to_ghidra, from_ghidra); nothing else uses those addresses.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_EXE = REPO / "assets" / "hp2" / "Highway Patrol II" / "hp.prg"
DATA_DIR = REPO / "assets" / "hp2" / "Highway Patrol II" / "DISK2_2"

HUNK_HEADER = 0x3F3
HUNK_CODE = 0x3E9
HUNK_DATA = 0x3EA
HUNK_BSS = 0x3EB
HUNK_RELOC32 = 0x3EC
HUNK_END = 0x3F2

# Where the Ghidra project loads each hunk.
GHIDRA_HUNK_ADDRESSES = (0x0021F000, 0x0022D8A8)
BASE = GHIDRA_HUNK_ADDRESSES[0]


@dataclass(frozen=True, order=True)
class HunkOffset:
    """A place in hp.prg: hunk index and byte offset in that hunk."""

    hunk: int
    offset: int

    @classmethod
    def parse(cls, text: str) -> HunkOffset:
        """Parse ``hunk:offset`` with a hexadecimal offset (``1:2870``)."""
        hunk, offset = text.split(":")
        return cls(int(hunk), int(offset, 16))

    def __add__(self, delta: int) -> HunkOffset:
        return HunkOffset(self.hunk, self.offset + delta)

    def __str__(self) -> str:
        return f"{self.hunk}:{self.offset:04x}"


def to_ghidra(place: HunkOffset) -> int:
    """The Ghidra project's address of ``place``."""
    return GHIDRA_HUNK_ADDRESSES[place.hunk] + place.offset


def from_ghidra(address: int) -> HunkOffset:
    """The place at a Ghidra project address (the hunk whose start is the last one below it)."""
    hunk = max(index for index, start in enumerate(GHIDRA_HUNK_ADDRESSES) if start <= address)
    return HunkOffset(hunk, address - GHIDRA_HUNK_ADDRESSES[hunk])


@dataclass
class Hunk:
    index: int
    kind: int
    address: int
    data: bytearray


class Executable:
    def __init__(self, path: Path = DEFAULT_EXE, base: int = BASE) -> None:
        raw = path.read_bytes()
        pos = 0

        def long() -> int:
            nonlocal pos
            value = struct.unpack_from(">I", raw, pos)[0]
            pos += 4
            return value

        if long() != HUNK_HEADER:
            raise ValueError("not an AmigaDOS executable")
        while long():  # resident library names (none expected)
            pass
        count = long()
        first = long()
        last = long()
        sizes = [(long() & 0x3FFFFFFF) * 4 for _ in range(last - first + 1)]

        addresses = []
        address = base
        for size in sizes:
            addresses.append(address)
            address += size

        self.hunks: list[Hunk] = []
        relocs: list[tuple[int, int, list[int]]] = []
        index = 0
        while pos < len(raw) and index < count:
            kind = long() & 0x3FFFFFFF
            if kind in (HUNK_CODE, HUNK_DATA):
                n = long() * 4
                data = bytearray(raw[pos : pos + n])
                data += bytes(sizes[index] - n)
                pos += n
                self.hunks.append(Hunk(index, kind, addresses[index], data))
            elif kind == HUNK_BSS:
                long()
                self.hunks.append(Hunk(index, kind, addresses[index], bytearray(sizes[index])))
            elif kind == HUNK_RELOC32:
                while True:
                    n = long()
                    if n == 0:
                        break
                    target = long()
                    offsets = [long() for _ in range(n)]
                    relocs.append((index, target, offsets))
            elif kind == HUNK_END:
                index += 1
            else:
                raise ValueError(f"unsupported hunk type {kind:#x}")

        for hunk_index, target, offsets in relocs:
            data = self.hunks[hunk_index].data
            for offset in offsets:
                value = struct.unpack_from(">I", data, offset)[0]
                struct.pack_into(">I", data, offset, value + addresses[target])

    def read(self, place: HunkOffset, length: int) -> bytes:
        # Hunks are loaded back to back, so a read may run from one into the next.
        address = self.hunks[place.hunk].address + place.offset
        out = bytearray()
        while length > 0:
            for hunk in self.hunks:
                start = address - hunk.address
                if 0 <= start < len(hunk.data):
                    chunk = hunk.data[start : start + length]
                    out += chunk
                    address += len(chunk)
                    length -= len(chunk)
                    break
            else:
                raise ValueError(f"{place} + {len(out):#x} is outside the executable")
        return bytes(out)

    def word(self, place: HunkOffset, signed: bool = False) -> int:
        return struct.unpack(">h" if signed else ">H", self.read(place, 2))[0]

    def long(self, place: HunkOffset) -> int:
        return struct.unpack(">I", self.read(place, 4))[0]

    def words(self, place: HunkOffset, count: int, signed: bool = True) -> list[int]:
        return list(struct.unpack(f">{count}{'h' if signed else 'H'}", self.read(place, count * 2)))

    def longs(self, place: HunkOffset, count: int) -> list[int]:
        return list(struct.unpack(f">{count}I", self.read(place, count * 4)))

    def pointers(self, place: HunkOffset, count: int) -> list[HunkOffset]:
        """``count`` relocated pointer longs from ``place``, as the places they point at."""
        return [self.locate(value) for value in self.longs(place, count)]

    def locate(self, address: int) -> HunkOffset:
        """The place a relocated pointer value points at."""
        for hunk in self.hunks:
            if hunk.address <= address < hunk.address + len(hunk.data):
                return HunkOffset(hunk.index, address - hunk.address)
        raise ValueError(f"pointer {address:#x} is outside the executable")
