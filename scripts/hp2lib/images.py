"""Decoders for the Highway Patrol II picture formats (.CPV, .IMG, .DIF, LETTRE*.BIN).

Every routine here mirrors a routine of hp.prg; its place (``hunk:offset``) is given in each
docstring. Format notes and evidence are in work/analysis/frontend.md.

Screen model (InitDisplay 1:0e90): 320x200, 4 bitplanes of 8000 bytes, 40 bytes per row,
plane n at offset n*8000. Colors are Amiga 12-bit words 0x0RGB. Picture files carry Atari ST
style palettes (3 bits per gun, 0..7) that PaletteSTToAmiga (0:1006) converts with the table at
0:1066.
"""

from __future__ import annotations

import struct
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from typing import TypeGuard

import numpy as np
from PIL import Image

SCREEN_WIDTH = 320
SCREEN_HEIGHT = 200
SCREEN_PLANES = 4
ROW_BYTES = 40
PLANE_BYTES = ROW_BYTES * SCREEN_HEIGHT  # 8000
SCREEN_BYTES = PLANE_BYTES * SCREEN_PLANES  # 32000
CPV_MAGIC = 0x1234
CPV_HEADER_BYTES = 2 + 32
# 0:1006 / 0:1066: 3-bit ST gun level -> 4-bit Amiga gun level.
ST_TO_AMIGA = (0x0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xF)

Palette = Sequence[int]
"""16 Amiga color words (0x0RGB, 4 bits per gun)."""
RowPalettes = Sequence[Sequence[int]]
"""One 16-entry Amiga palette per screen row (what a split-palette copper list produces)."""


class FormatError(ValueError):
    """Raised when a file does not match the layout the game's decoder expects."""


# --------------------------------------------------------------------------- palettes


def st_to_amiga(word: int) -> int:
    """Convert one ST palette word (0x0RGB, guns 0..7) to Amiga (guns 0..15) like 0:1006.

    The game's table has 8 entries; a gun value above 7 would index past it (into code bytes),
    so it is rejected here. No shipped file has one.
    """
    guns = ((word >> 8) & 0xF, (word >> 4) & 0xF, word & 0xF)
    if max(guns) > 7:
        raise FormatError(f"ST palette word {word:#06x} has a gun level above 7")
    r, g, b = (ST_TO_AMIGA[v] for v in guns)
    return (r << 8) | (g << 4) | b


def amiga_to_rgb(word: int) -> tuple[int, int, int]:
    """Expand an Amiga 0x0RGB color word to 8-bit RGB (each nibble times 17)."""
    return (((word >> 8) & 0xF) * 17, ((word >> 4) & 0xF) * 17, (word & 0xF) * 17)


def st_to_rgb(word: int) -> tuple[int, int, int]:
    """Expand an Atari ST 0x0RGB color word (3 bits per channel) to 8-bit RGB, each channel scaled to
    0..255 and rounded, as FromAtariSt in src/host/format/color.hpp."""
    red, green, blue = ((((word >> shift) & 0x7) * 255 + 3) // 7 for shift in (8, 4, 0))
    return red, green, blue


def fade_palette(words: Sequence[int], level: int) -> list[int]:
    """Subtract ``level`` from every gun, clamping at 0 (0:0d06, one palette segment)."""
    out = []
    for w in words:
        r = max(((w >> 8) & 0xF) - level, 0)
        g = max(((w >> 4) & 0xF) - level, 0)
        b = max((w & 0xF) - level, 0)
        out.append((r << 8) | (g << 4) | b)
    return out


@dataclass(frozen=True)
class PaletteSegment:
    """One segment of a copper palette list (format read by InstallPalette 1:19b4).

    ``start_row`` is the screen row the colors take effect on: segment 0 is loaded before the
    display window (line 0x2c), segment k waits for line 0x2c + sum of previous ``lines``.
    ``offset`` is where the segment starts, counted from the start of the list.
    """

    offset: int
    first: int
    colors: tuple[int, ...]
    lines: int
    start_row: int


def parse_palette_list(data: bytes, offset: int = 0) -> list[PaletteSegment]:
    """Parse {word first, word count, word lines-to-next (0 = last), word rgb[count]}... ."""
    segments: list[PaletteSegment] = []
    row = 0
    pos = offset
    while True:
        first, count, lines = struct.unpack_from(">HHH", data, pos)
        if first + count > 32:
            raise FormatError(f"palette segment at +{pos:#x} writes colors {first}..{first + count - 1}")
        colors = struct.unpack_from(f">{count}H", data, pos + 6)
        segments.append(PaletteSegment(pos - offset, first, tuple(colors), lines, row))
        pos += 6 + 2 * count
        if lines == 0:
            return segments
        row += lines


def row_palettes(segments: Sequence[PaletteSegment], st: bool = False, height: int = SCREEN_HEIGHT) -> list[list[int]]:
    """Return the 16 colors in effect on each screen row for a parsed palette list.

    ``st`` converts every color with :func:`st_to_amiga` first (lists the game passes through
    0:1006 before installing them). Colors not set by segment 0 start at 0.
    """
    current = [0] * 16
    rows: list[list[int]] = []
    pending = list(segments)
    for row in range(height):
        while pending and pending[0].start_row <= row:
            seg = pending.pop(0)
            for i, c in enumerate(seg.colors):
                if seg.first + i < 16:
                    current[seg.first + i] = st_to_amiga(c) if st else c
        rows.append(list(current))
    return rows


# --------------------------------------------------------------------------- planar helpers


def planes_to_indices(data: bytes, width_words: int, height: int, plane_map: Sequence[int]) -> np.ndarray:
    """Combine consecutively stored bitplanes into color indices.

    ``data`` holds len(plane_map) planes of height rows x width_words words each;
    stored plane k supplies bit ``plane_map[k]`` of the color index.
    """
    row_bytes = width_words * 2
    plane_size = row_bytes * height
    need = plane_size * len(plane_map)
    if len(data) < need:
        raise FormatError(f"plane data is {len(data)} bytes, need {need}")
    out = np.zeros((height, width_words * 16), dtype=np.uint8)
    for k, bit in enumerate(plane_map):
        plane = np.frombuffer(data, dtype=np.uint8, count=plane_size, offset=k * plane_size)
        bits = np.unpackbits(plane.reshape(height, row_bytes), axis=1)
        out |= (bits << bit).astype(np.uint8)
    return out


def screen_to_indices(planes: bytes) -> np.ndarray:
    """Convert a 32000-byte Amiga screen (4 planes of 8000 bytes) to a 200x320 index array."""
    return planes_to_indices(planes, ROW_BYTES // 2, SCREEN_HEIGHT, (0, 1, 2, 3))


def indices_to_screen(indices: np.ndarray) -> bytes:
    """Inverse of :func:`screen_to_indices`."""
    out = bytearray()
    for bit in range(SCREEN_PLANES):
        plane = ((indices >> bit) & 1).astype(np.uint8)
        out += np.packbits(plane, axis=1).tobytes()
    return bytes(out)


def st_interleaved_to_planar(buf: bytes) -> bytes:
    """ST low-res layout (per 16 pixels: plane0..plane3 words) to Amiga planes (0:1124)."""
    if len(buf) < SCREEN_BYTES:
        raise FormatError("ST screen buffer shorter than 32000 bytes")
    words = np.frombuffer(buf, dtype=">u2", count=SCREEN_BYTES // 2).reshape(SCREEN_HEIGHT, 20, 4)
    return np.ascontiguousarray(words.transpose(2, 0, 1)).astype(">u2").tobytes()


def planar_to_st_interleaved(planes: bytes) -> bytes:
    """Amiga planes to ST interleaved layout (loop at 0:ac52 in main)."""
    words = np.frombuffer(planes, dtype=">u2", count=SCREEN_BYTES // 2).reshape(4, SCREEN_HEIGHT, 20)
    return np.ascontiguousarray(words.transpose(1, 2, 0)).astype(">u2").tobytes()


# --------------------------------------------------------------------------- .CPV


@dataclass(frozen=True)
class CpvPicture:
    """A decoded .CPV picture: the 16 ST palette words of the header and the 32000-byte screen."""

    palette_st: tuple[int, ...]
    planes: bytes
    consumed: int
    """Bytes of the file the decoder read (it stops as soon as the screen is full)."""

    @property
    def palette(self) -> list[int]:
        """Header palette converted to Amiga color words (0:1006)."""
        return [st_to_amiga(w) for w in self.palette_st]

    def indices(self) -> np.ndarray:
        """200x320 color index array."""
        return screen_to_indices(self.planes)


def decode_cpv(data: bytes) -> CpvPicture:
    """Decode a .CPV file like 0:0f6c / 0:0fd0.

    Layout: word 0x1234, 16 ST palette words, then a byte-run stream. Control byte c:
    bit 7 clear -> repeat the next byte c times; bit 7 set -> copy (c & 0x7f) literal bytes.
    Output bytes fill each plane column by column: byte column 0 rows 0..199, column 1, ...
    column 39, then the next plane; decoding stops when plane 3 is full (mid-run if need be).
    """
    if len(data) < CPV_HEADER_BYTES or struct.unpack_from(">H", data, 0)[0] != CPV_MAGIC:
        raise FormatError("missing 0x1234 CPV magic")
    palette = struct.unpack_from(">16H", data, 2)
    out = bytearray(SCREEN_BYTES)
    # Destination byte for output position n: plane = n // 8000, column-major inside a plane.
    order = np.arange(PLANE_BYTES).reshape(ROW_BYTES, SCREEN_HEIGHT)  # [col][row]
    col_major = (order % SCREEN_HEIGHT) * ROW_BYTES + order // SCREEN_HEIGHT
    stream = bytearray()
    pos = CPV_HEADER_BYTES
    while len(stream) < SCREEN_BYTES:
        if pos >= len(data):
            raise FormatError(f"CPV stream ends after {len(stream)} of {SCREEN_BYTES} bytes")
        c = data[pos]
        pos += 1
        n = c & 0x7F if c & 0x80 else c
        if n == 0:
            # dbf with count -1 would run 65536 times; no shipped file does this.
            raise FormatError(f"zero-length CPV control byte at {pos - 1:#x}")
        if c & 0x80:
            stream += data[pos : pos + n]
            pos += n
        else:
            stream += bytes([data[pos]]) * n
            pos += 1
    stream = stream[:SCREEN_BYTES]
    src = np.frombuffer(bytes(stream), dtype=np.uint8)
    dst = np.frombuffer(out, dtype=np.uint8).copy()
    for p in range(SCREEN_PLANES):
        dst[p * PLANE_BYTES + col_major.reshape(-1)] = src[p * PLANE_BYTES : (p + 1) * PLANE_BYTES]
    return CpvPicture(tuple(palette), dst.tobytes(), min(pos, len(data)))


# --------------------------------------------------------------------------- .IMG bob banks


@dataclass(frozen=True)
class Bob:
    """One image of a bob bank (layout read by BlitBob 1:001c)."""

    index: int
    """1-based index, as passed to BlitBob."""
    flags: int
    width_words: int
    height: int
    origin_x: int
    origin_y: int
    data: bytes
    """Plane data: stored planes one after another, each height rows of width_words words."""

    @property
    def width(self) -> int:
        return self.width_words * 16

    @property
    def stored_planes(self) -> int:
        """flags bits 0-3."""
        return self.flags & 0xF

    @property
    def plane_mask(self) -> int:
        """flags bits 8-11: screen planes that receive the stored planes, lowest first."""
        return (self.flags >> 8) & 0xF

    @property
    def plane_map(self) -> list[int]:
        """Screen plane for each stored plane, in storage order."""
        targets = [p for p in range(SCREEN_PLANES) if self.plane_mask & (1 << p)]
        return targets[: self.stored_planes]

    def indices(self) -> np.ndarray:
        """height x width color indices; color 0 is transparent when BlitBob draws masked."""
        return planes_to_indices(self.data, self.width_words, self.height, self.plane_map)


def parse_bob_bank(data: bytes) -> list[Bob]:
    """Parse a bob bank: word count, word offsets[count] from bank start, then images.

    Image: word flags, word width in words, word height, word origin x, word origin y,
    then flags&15 planes of width*2*height bytes.
    """
    (count,) = struct.unpack_from(">H", data, 0)
    offsets = struct.unpack_from(f">{count}H", data, 2)
    bobs: list[Bob] = []
    for i, off in enumerate(offsets):
        flags, w, h, ox, oy = struct.unpack_from(">HHHhh", data, off)
        size = (flags & 0xF) * w * 2 * h
        body = data[off + 10 : off + 10 + size]
        if len(body) != size:
            raise FormatError(f"bob {i + 1} at {off:#x} runs past the end of the bank")
        bobs.append(Bob(i + 1, flags, w, h, ox, oy, body))
    return bobs


def blit_bob(screen: np.ndarray, bob: Bob, x: int, y: int, masked: bool = True, hotspot: bool = False) -> None:
    """Draw ``bob`` into a 200x320 index array the way BlitBob does (no clip rectangle).

    Masked: pixels with color 0 keep the background; screen planes absent from the image's
    plane mask are cleared under the mask. Opaque: the whole rectangle is replaced.
    """
    if hotspot:
        x -= bob.origin_x
        y -= bob.origin_y
    img = bob.indices()
    h, w = img.shape
    y0, y1 = max(y, 0), min(y + h, screen.shape[0])
    x0, x1 = max(x, 0), min(x + w, screen.shape[1])
    if y0 >= y1 or x0 >= x1:
        return
    part = img[y0 - y : y1 - y, x0 - x : x1 - x]
    region = screen[y0:y1, x0:x1]
    if masked:
        region[part != 0] = part[part != 0]
    else:
        region[:, :] = part


# --------------------------------------------------------------------------- .DIF


@dataclass(frozen=True)
class DifFrame:
    """One XOR delta of a .DIF file: (byte offset, words) runs into an ST-layout screen."""

    number: int
    runs: tuple[tuple[int, tuple[int, ...]], ...]


def parse_dif(data: bytes) -> list[DifFrame]:
    """Parse a .DIF animation (read by 0:1102).

    Layout: long frame count n, long offsets[n] (frame k at offsets[k-1], counted from file
    start, i.e. table entry k of the long array starting at the count), then per frame
    repeated {word count (0 = end), word byte offset, word xor[count]}.
    """
    (n,) = struct.unpack_from(">I", data, 0)
    offsets = struct.unpack_from(f">{n}I", data, 4)
    frames: list[DifFrame] = []
    for k, off in enumerate(offsets, start=1):
        pos = off
        runs = []
        while True:
            (count,) = struct.unpack_from(">H", data, pos)
            pos += 2
            if count == 0:
                break
            (target,) = struct.unpack_from(">H", data, pos)
            words = struct.unpack_from(f">{count}H", data, pos + 2)
            pos += 2 + 2 * count
            if target + 2 * count > SCREEN_BYTES:
                raise FormatError(f"DIF frame {k} writes past the screen ({target:#x}+{2 * count})")
            runs.append((target, tuple(words)))
        frames.append(DifFrame(k, tuple(runs)))
    return frames


def apply_dif_frame(buf: bytearray, frame: DifFrame) -> None:
    """XOR one delta into a 32000-byte ST-layout buffer (0:1102)."""
    for target, words in frame.runs:
        for i, w in enumerate(words):
            pos = target + 2 * i
            old = (buf[pos] << 8) | buf[pos + 1]
            new = old ^ w
            buf[pos] = new >> 8
            buf[pos + 1] = new & 0xFF


@dataclass(frozen=True)
class PixelRun:
    """A delta run as pixel XOR masks, as the port applies it: whole 16-pixel groups from pixel
    `offset` (row * 320 + x) on."""

    offset: int
    masks: bytes


def dif_pixel_runs(frame: DifFrame) -> list[PixelRun]:
    """The frame's runs from plane words (4 interleaved per 16 pixels) to pixel masks
    (src/host/format/dif.cpp): XORing plane bits is XORing color index bits."""
    runs: list[PixelRun] = []
    for target, words in frame.runs:
        first_group = target // 8
        last_group = (target + 2 * len(words) - 2) // 8
        masks = bytearray((last_group - first_group + 1) * 16)
        for index, word in enumerate(words):
            byte = target + 2 * index
            plane_bit = 1 << ((byte % 8) // 2)
            first_pixel = (byte // 8 - first_group) * 16
            for pixel in range(16):
                if word & (0x8000 >> pixel):
                    masks[first_pixel + pixel] ^= plane_bit
        runs.append(PixelRun(offset=first_group * 16, masks=bytes(masks)))
    return runs


def parse_dif_sequence(data: bytes) -> list[tuple[int, int]]:
    """Parse the play list 0:106e walks: {word frame (0 = end), word delay}..."""
    seq: list[tuple[int, int]] = []
    pos = 0
    while True:
        (frame,) = struct.unpack_from(">H", data, pos)
        if frame == 0:
            return seq
        (delay,) = struct.unpack_from(">H", data, pos + 2)
        seq.append((frame, delay))
        pos += 4


def play_dif(
    base_planes: bytes, frames: Sequence[DifFrame], sequence: Sequence[tuple[int, int]]
) -> Iterator[tuple[int, int, int, bytes]]:
    """Replay 0:106e: yield (step, frame number, delay, Amiga planes) after each delta.

    ``base_planes`` is the starting picture in Amiga plane layout; main converts the decoded
    PRESENT.CPV to ST layout (0:ac52) before the player XORs deltas into it.
    """
    buf = bytearray(planar_to_st_interleaved(base_planes))
    by_number = {f.number: f for f in frames}
    for step, (number, delay) in enumerate(sequence, start=1):
        apply_dif_frame(buf, by_number[number])
        yield step, number, delay, st_interleaved_to_planar(bytes(buf))


# --------------------------------------------------------------------------- fonts


def decode_font(data: bytes) -> list[np.ndarray]:
    """Decode an 8x8 4-plane font (LETTRE1/2.BIN) as drawn by 0:0be2.

    Glyph g (character 0x20 + g) is 32 bytes: for each of 8 rows, one byte per plane 0..3.
    """
    glyphs = []
    for g in range(len(data) // 32):
        raw = np.frombuffer(data, dtype=np.uint8, count=32, offset=g * 32).reshape(8, 4)
        bits = np.unpackbits(raw[:, :, None], axis=2)  # rows x planes x 8
        idx = np.zeros((8, 8), dtype=np.uint8)
        for p in range(4):
            idx |= (bits[:, p, :] << p).astype(np.uint8)
        glyphs.append(idx)
    return glyphs


# --------------------------------------------------------------------------- rendering

PaletteSpec = Palette | RowPalettes


def render(
    indices: np.ndarray,
    palette: PaletteSpec,
    transparent_zero: bool = False,
    first_row: int = 0,
) -> Image.Image:
    """Render an index array.

    ``palette`` is either 16 Amiga words (gives a mode "P" image) or one palette per screen
    row (gives RGB/RGBA; image row r uses ``palette[first_row + r]``). With
    ``transparent_zero`` color 0 becomes transparent, as for masked bobs.
    """
    if is_single_palette(palette):
        return render_indexed(indices, palette, transparent_zero)
    if not is_row_palettes(palette):
        raise TypeError("palette must be 16 colors or one 16-color palette per row")
    return render_rows(indices, palette, transparent_zero, first_row)


def is_single_palette(palette: PaletteSpec) -> TypeGuard[Palette]:
    return all(isinstance(color, int) for color in palette)


def is_row_palettes(palette: PaletteSpec) -> TypeGuard[RowPalettes]:
    return all(not isinstance(row, int) for row in palette)


def render_indexed(indices: np.ndarray, palette: Palette, transparent_zero: bool) -> Image.Image:
    """Mode "P" image with one 16-color palette."""
    img = Image.fromarray(indices, mode="P")
    flat: list[int] = []
    for color in palette:
        flat.extend(amiga_to_rgb(color))
    img.putpalette(flat + [0] * (768 - len(flat)))
    if transparent_zero:
        img.info["transparency"] = 0
    return img


def render_rows(indices: np.ndarray, palette: RowPalettes, transparent_zero: bool, first_row: int) -> Image.Image:
    """RGB(A) image where image row r uses palette[first_row + r] (copper palettes)."""
    rows = np.array(
        [
            [amiga_to_rgb(color) for color in palette[min(first_row + row, len(palette) - 1)]]
            for row in range(indices.shape[0])
        ],
        dtype=np.uint8,
    )  # h x 16 x 3
    rgb = np.take_along_axis(rows, indices[:, :, None].astype(np.intp).repeat(3, axis=2), axis=1)
    if transparent_zero:
        alpha = np.where(indices == 0, 0, 255).astype(np.uint8)[:, :, None]
        return Image.fromarray(np.concatenate([rgb, alpha], axis=2), mode="RGBA")
    return Image.fromarray(rgb, mode="RGB")


def contact_sheet(
    images: Sequence[Image.Image],
    columns: int = 8,
    pad: int = 4,
    background: tuple[int, int, int, int] = (96, 0, 96, 255),
) -> Image.Image:
    """Lay images out on a grid for quick inspection.

    The default opaque purple background shows where color 0 (transparent) is, so it is not
    mistaken for black; pass (0, 0, 0, 0) for a transparent sheet.
    """
    images = [im.convert("RGBA") for im in images]
    cols = max(1, min(columns, len(images)))
    rows = (len(images) + cols - 1) // cols
    cell_w = max(im.width for im in images) + pad
    row_h = [max(im.height for im in images[r * cols : (r + 1) * cols]) + pad for r in range(rows)]
    sheet = Image.new("RGBA", (cols * cell_w + pad, sum(row_h) + pad), background)
    y = pad
    for r in range(rows):
        for c, im in enumerate(images[r * cols : (r + 1) * cols]):
            sheet.paste(im, (pad + c * cell_w, y), im)
        y += row_h[r]
    return sheet
