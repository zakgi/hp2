"""Decoders for the Highway Patrol II sound files: the .MUS music module and the IFF 8SVX sounds.

Each mirrors the C++ decoder of the same file (src/host/format/music_file.cpp, iff_8svx.cpp);
the formats are described in docs/formats.md ("Sounds: .SND", "Music: .MUS").
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

SAMPLE_COUNT = 15
SIZE_TABLE_BYTES = SAMPLE_COUNT * 4
TIMER_OFFSET = 0x4
SAMPLE_HEADERS_OFFSET = 0x14
SAMPLE_HEADER_BYTES = 30
SAMPLE_NAME_BYTES = 22
SONG_LENGTH_OFFSET = 0x1D6
POSITIONS_OFFSET = 0x1D8
POSITION_COUNT = 128
PATTERNS_OFFSET = 0x258
PATTERN_BYTES = 64 * 4 * 4
FULL_VOLUME = 64


class FormatError(ValueError):
    """Raised when a file does not match the layout its decoder expects."""


@dataclass(frozen=True)
class ModuleNote:
    period: int
    sample: int
    effect: int
    parameter: int


@dataclass(frozen=True)
class ModuleSample:
    """An instrument: its bytes are the module's sample data [offset, offset + size)."""

    offset: int
    size: int
    length: int
    loop_start: int
    loop_length: int
    volume: int


@dataclass(frozen=True)
class MusicFile:
    timer: int
    positions: bytes
    notes: tuple[ModuleNote, ...]
    sample_data: bytes
    samples: tuple[ModuleSample, ...]

    @staticmethod
    def from_data(data: bytes) -> MusicFile:
        """15 long instrument sizes, a 15-sample Soundtracker module, then the instruments."""
        if len(data) < SIZE_TABLE_BYTES + PATTERNS_OFFSET:
            raise FormatError(f"music file of {len(data)} bytes is too short")
        sizes = struct.unpack_from(f">{SAMPLE_COUNT}I", data, 0)
        module = data[SIZE_TABLE_BYTES:]
        song_length = module[SONG_LENGTH_OFFSET]
        if not 0 < song_length <= POSITION_COUNT:
            raise FormatError(f"song length {song_length}")
        positions = module[POSITIONS_OFFSET : POSITIONS_OFFSET + song_length]
        samples_offset = PATTERNS_OFFSET + (max(positions) + 1) * PATTERN_BYTES
        if len(module) < samples_offset + sum(sizes):
            raise FormatError("patterns or instruments run past the end of the file")
        notes = tuple(
            ModuleNote(period, control >> 4, control & 0xF, parameter)
            for period, control, parameter in struct.iter_unpack(">HBB", module[PATTERNS_OFFSET:samples_offset])
        )
        samples: list[ModuleSample] = []
        offset = 0
        for index, size in enumerate(sizes):
            header = SAMPLE_HEADERS_OFFSET + index * SAMPLE_HEADER_BYTES + SAMPLE_NAME_BYTES
            length, volume, loop_start, loop_length = struct.unpack_from(">HHHH", module, header)
            samples.append(
                ModuleSample(offset, size, 2 * length, loop_start, 2 * loop_length, min(volume & 0xFF, FULL_VOLUME))
            )
            offset += size
        return MusicFile(
            timer=struct.unpack_from(">H", module, TIMER_OFFSET)[0],
            positions=bytes(positions),
            notes=notes,
            sample_data=bytes(module[samples_offset : samples_offset + sum(sizes)]),
            samples=tuple(samples),
        )


@dataclass(frozen=True)
class Sound8Svx:
    """An uncompressed IFF 8SVX sound: the VHDR fields the game's files use and the BODY."""

    one_shot_samples: int
    repeat_samples: int
    rate: int
    samples: bytes

    @staticmethod
    def from_data(data: bytes) -> Sound8Svx | None:
        """The sound, or None when `data` is not a FORM 8SVX with a VHDR and uncompressed BODY."""
        if len(data) < 12 or data[0:4] != b"FORM" or data[8:12] != b"8SVX":
            return None
        form_size = struct.unpack_from(">I", data, 4)[0]
        end = min(len(data), 8 + form_size)
        position = 12
        voice: tuple[int, ...] | None = None
        body: bytes | None = None
        while position + 8 <= end:
            chunk_id, size = struct.unpack_from(">4sI", data, position)
            position += 8
            if size > end - position:
                break
            payload = data[position : position + size]
            if chunk_id == b"VHDR" and size >= 20:
                voice = struct.unpack_from(">IIIHBBI", payload, 0)
            elif chunk_id == b"BODY":
                body = bytes(payload)
            position += size + (size % 2)
        if voice is None or body is None or voice[5] != 0:
            return None
        return Sound8Svx(one_shot_samples=voice[0], repeat_samples=voice[1], rate=voice[3], samples=body)
