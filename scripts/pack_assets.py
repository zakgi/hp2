"""Pack the decoded game assets into the firmware's flash partition image.

    pack_assets.py --partitions src/boards/<board>/partitions.json --out-dir build/assets \\
        [--game "assets/hp2/Highway Patrol II"] [--layout-header src/target/flash/asset_layout.hpp]

Writes assets.bin, the image laid out as the engine's types are in memory (src/core/*.hpp; the
record layouts are the contract src/target/flash/asset_image.hpp checks), assets.uf2 targeting the
"assets" partition, and the layout header: the image size and SHA-256 the firmware checks before
using the image, the section offsets, and FlashAssets(base), the engine's EngineAssets over the
image at `base`. Sections are 4-byte aligned. The host test compares the image with what the C++
asset manager decodes from the disk, so the two decoders cannot drift apart unnoticed.

The contents follow EngineAssets (src/core/engine_assets.hpp), in the order of its enums.
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import struct
import sys
from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from hp2lib import audio, images, world
from hp2lib.exe import Executable, HunkOffset
from hp2lib.partition_table import Partition, find_partition, load_partitions
from hp2lib.uf2 import UF2Packer

LOGGER = logging.getLogger("pack_assets")

DEFAULT_GAME = Path(__file__).resolve().parents[1] / "assets" / "hp2" / "Highway Patrol II"
SECTION_ALIGNMENT = 4
SCREEN_WIDTH = 320
SCREEN_HEIGHT = 200
COLOR_REGISTERS = 16
NTSC_PAULA_CLOCK_HZ = 3_579_545
AMIGA_FORMAT = 0
ATARI_ST_FORMAT = 1

# In EnginePicture order.
PICTURES = (
    "LOGO.CPV",
    "PRESENT.CPV",
    "BUREAU.CPV",
    "STATION.CPV",
    "PAGE_F1.CPV",
    "PAGE_F2.CPV",
    "PAGE_F3.CPV",
    "PAGE_F4.CPV",
    "PAGE_F5.CPV",
    "PAGE_F6.CPV",
)


@dataclass(frozen=True)
class PaletteSource:
    """A picture's header palette (`picture`, an index into PICTURES) or a list in hp.prg."""

    picture: int | None = None
    place: HunkOffset | None = None
    color_format: int = AMIGA_FORMAT


# In EnginePalette order.
PALETTES = (
    PaletteSource(picture=0),  # logo
    PaletteSource(picture=3),  # station
    PaletteSource(picture=4),  # spin-out
    PaletteSource(picture=5),  # tires gone
    PaletteSource(picture=6),  # out of fuel
    PaletteSource(picture=7),  # overheated
    PaletteSource(picture=8),  # wrecked
    PaletteSource(picture=9),  # arrest
    PaletteSource(place=HunkOffset(1, 0x28E0), color_format=ATARI_ST_FORMAT),  # titlePalette
    PaletteSource(place=HunkOffset(0, 0xCA94)),  # officePalette
    PaletteSource(place=HunkOffset(0, 0xCA6E)),  # officePaletteDim
    PaletteSource(place=HunkOffset(0, 0x7766)),  # scorePalette
    PaletteSource(place=HunkOffset(0, 0xA520)),  # viewPalette
    PaletteSource(place=HunkOffset(0, 0xA734)),  # viewPaletteRed
)

# In EngineBank order.
BANKS = (
    "NAME.IMG",
    "BUREAU.IMG",
    "STATION.IMG",
    "GAME_SCO.IMG",
    "BALLE.IMG",
    "CACTUS.IMG",
    "BUISSON.IMG",
    "CAILLOUX.IMG",
    "DEC_FOND.IMG",
    "DES_TABB.IMG",
    "PAN_POT.IMG",
    "PAN_GAU.IMG",
    "PAN_CRO.IMG",
    "PAN_DRO.IMG",
    "PAN_ARR.IMG",
    "PAN_PRO.IMG",
    "PST_FLG.IMG",
    "PST_FLD.IMG",
    "PST_PRO.IMG",
    "PST_STA.IMG",
    "VOITURE0.IMG",
    "VOITURE1.IMG",
    "VOITURE2.IMG",
    "VOITURE3.IMG",
    "VOITURE4.IMG",
    "VOITURE5.IMG",
    "VOITURE6.IMG",
)

# In EngineFont order.
FONTS = ("LETTRE1.BIN", "LETTRE2.BIN")


@dataclass(frozen=True)
class SoundSource:
    """An 8SVX sound (period 0) or a bare one played at its period on the NTSC clock."""

    name: str
    period: int = 0
    looped: bool = False


# In EngineSound order (src/host/asset_manager.cpp, kSoundSources).
SOUNDS = (
    SoundSource("MOTEUR.SND"),
    SoundSource("SIRENE.SND", period=864, looped=True),
    SoundSource("TIR.SND", period=427),
    SoundSource("DERAP.SND"),
    SoundSource("CHOC.SND", period=640),
)

TITLE_ANIMATION = "PRESENT.DIF"
TITLE_PLAY_LIST = HunkOffset(1, 0x2870)
TITLE_MUSIC = "HIGHWAY.MUS"
ROAD_MAP = "CARTE.BIN"
SCENERY = "COOR_OBJ.BIN"
# The road outlines, one per road cell type, in this build of hp.prg (docs/formats.md, "Road shapes
# in the executable"); the same places as kRoadShapePlaces in src/host/asset_manager.cpp.
ROAD_SHAPES = (
    HunkOffset(0, 0x7846),
    HunkOffset(0, 0x7848),
    HunkOffset(0, 0x7876),
    HunkOffset(0, 0x78A4),
    HunkOffset(0, 0x79E6),
    HunkOffset(0, 0x7B28),
    HunkOffset(0, 0x7C6A),
    HunkOffset(0, 0x7DAC),
    HunkOffset(0, 0x7DF2),
    HunkOffset(0, 0x7E38),
    HunkOffset(0, 0x7E7E),
    HunkOffset(0, 0x7EE8),
    HunkOffset(0, 0x7F46),
)

# Record layouts of the engine types (little-endian, as on the RP2350 and the hosts); the C++ side
# static_asserts the same sizes and offsets (src/target/flash/asset_image.hpp).
PALETTE_SEGMENT = struct.Struct(f"<HBB{COLOR_REGISTERS * 3}B")
SPRITE_RANGE = struct.Struct("<IHHhh")
XOR_RUN = struct.Struct("<III")
XOR_FRAME = struct.Struct("<III")
ANIMATION_STEP = struct.Struct("<HH")
MODULE_SAMPLE = struct.Struct("<IIIIIB3x")
MODULE_NOTE = struct.Struct("<HBBBx")
PLACED_OBJECT = struct.Struct("<hhhHH")
SHAPE_POINT = struct.Struct("<hh")


class FormatError(ValueError):
    """Raised when a game file does not decode as the engine expects."""


@dataclass(frozen=True)
class Section:
    name: str
    offset: int
    count: int


@dataclass(frozen=True)
class Span:
    """A run of `count` records starting `offset` bytes into the image."""

    offset: int
    count: int


@dataclass
class Image:
    """The image under construction: sections appended 4-byte aligned."""

    data: bytearray = field(default_factory=bytearray)
    sections: list[Section] = field(default_factory=list)

    def append(self, name: str, payload: bytes, count: int) -> int:
        """Appends a section and returns its offset."""
        self.data.extend(bytes(-len(self.data) % SECTION_ALIGNMENT))
        offset = len(self.data)
        self.sections.append(Section(name=name, offset=offset, count=count))
        self.data.extend(payload)
        return offset


@dataclass(frozen=True)
class SoundEntry:
    samples: Span
    rate: int
    loop_start: int
    loop_length: int


@dataclass(frozen=True)
class BankEntry:
    pixels: Span
    sprites: Span


@dataclass
class Layout:
    """Where everything of EngineAssets lies in the image."""

    pictures: list[Span] = field(default_factory=list)
    palettes: list[Span] = field(default_factory=list)
    banks: list[BankEntry] = field(default_factory=list)
    fonts: list[Span] = field(default_factory=list)
    sounds: list[SoundEntry] = field(default_factory=list)
    animation: dict[str, Span] = field(default_factory=dict)
    music: dict[str, Span] = field(default_factory=dict)
    music_timer: int = 0
    road_map: Span = Span(0, 0)
    scenery_objects: Span = Span(0, 0)
    scenery_ranges: list[tuple[int, int]] = field(default_factory=list)
    road_shape_points: Span = Span(0, 0)
    road_shape_ranges: list[tuple[int, int]] = field(default_factory=list)


class Game:
    """The extracted game: hp.prg and DISK2_2/, names matched without regard to case."""

    def __init__(self, root: Path) -> None:
        self.executable = Executable(root / "hp.prg")
        self.files = {path.name.upper(): path for path in (root / "DISK2_2").iterdir()}

    def read(self, name: str) -> bytes:
        path = self.files.get(name.upper())
        if path is None:
            raise FormatError(f"the game has no DISK2_2/{name}")
        return path.read_bytes()


def concatenated(parts: Iterable[bytes]) -> bytes:
    return b"".join(parts)


def pack_pictures(image: Image, game: Game, layout: Layout) -> list[tuple[int, ...]]:
    """Every picture as color indices; returns the header palettes."""
    pixels = bytearray()
    headers: list[tuple[int, ...]] = []
    for name in PICTURES:
        picture = images.decode_cpv(game.read(name))
        layout.pictures.append(Span(len(pixels), SCREEN_WIDTH * SCREEN_HEIGHT))
        pixels.extend(picture.indices().tobytes())
        headers.append(picture.palette_st)
    base = image.append("picture_pixels", bytes(pixels), len(pixels))
    layout.pictures = [Span(base + span.offset, span.count) for span in layout.pictures]
    return headers


def palette_colors(words: Sequence[int], color_format: int) -> list[int]:
    """The color words decoded to RGB channels, COLOR_REGISTERS colors, the missing ones black."""
    convert = images.st_to_rgb if color_format == ATARI_ST_FORMAT else images.amiga_to_rgb
    channels = [channel for word in words for channel in convert(word)]
    return channels + [0] * (COLOR_REGISTERS * 3 - len(channels))


def palette_segments(game: Game, source: PaletteSource, headers: list[tuple[int, ...]]) -> list[bytes]:
    if source.picture is not None:
        colors = palette_colors(headers[source.picture], ATARI_ST_FORMAT)
        return [PALETTE_SEGMENT.pack(0, 0, COLOR_REGISTERS, *colors)]
    assert source.place is not None
    records: list[bytes] = []
    for segment in images.parse_palette_list(game.executable.read(source.place, 0x400)):
        if segment.first + len(segment.colors) > COLOR_REGISTERS:
            raise FormatError(f"palette list at {source.place} writes past register {COLOR_REGISTERS - 1}")
        colors = palette_colors(segment.colors, source.color_format)
        records.append(PALETTE_SEGMENT.pack(segment.start_row, segment.first, len(segment.colors), *colors))
    return records


def pack_palettes(image: Image, game: Game, headers: list[tuple[int, ...]], layout: Layout) -> None:
    records: list[bytes] = []
    spans: list[Span] = []
    for source in PALETTES:
        segments = palette_segments(game, source, headers)
        spans.append(Span(len(records), len(segments)))
        records.extend(segments)
    base = image.append("palette_segments", concatenated(records), len(records))
    layout.palettes = [Span(base + span.offset * PALETTE_SEGMENT.size, span.count) for span in spans]


def pack_banks(image: Image, game: Game, layout: Layout) -> None:
    """Every bank's images as color indices, each bank's sprite offsets from its own first pixel."""
    pixels = bytearray()
    sprites: list[bytes] = []
    entries: list[tuple[int, int, int, int]] = []
    for name in BANKS:
        bank_start = len(pixels)
        sprite_start = len(sprites)
        for bob in images.parse_bob_bank(game.read(name)):
            indices = bob.indices().tobytes()
            sprites.append(
                SPRITE_RANGE.pack(len(pixels) - bank_start, bob.width, bob.height, bob.origin_x, bob.origin_y)
            )
            pixels.extend(indices)
        entries.append((bank_start, len(pixels) - bank_start, sprite_start, len(sprites) - sprite_start))
    pixel_base = image.append("bank_pixels", bytes(pixels), len(pixels))
    sprite_base = image.append("bank_sprites", concatenated(sprites), len(sprites))
    layout.banks = [
        BankEntry(Span(pixel_base + start, count), Span(sprite_base + first * SPRITE_RANGE.size, sprite_count))
        for start, count, first, sprite_count in entries
    ]


def pack_fonts(image: Image, game: Game, layout: Layout) -> None:
    pixels = bytearray()
    spans: list[Span] = []
    for name in FONTS:
        glyphs = concatenated(glyph.tobytes() for glyph in images.decode_font(game.read(name)))
        spans.append(Span(len(pixels), len(glyphs)))
        pixels.extend(glyphs)
    base = image.append("font_pixels", bytes(pixels), len(pixels))
    layout.fonts = [Span(base + span.offset, span.count) for span in spans]


def pack_sounds(image: Image, game: Game, layout: Layout) -> None:
    samples = bytearray()
    entries: list[tuple[int, int, int, int, int]] = []
    for source in SOUNDS:
        data = game.read(source.name)
        sound = audio.Sound8Svx.from_data(data)
        if sound is not None:
            body = sound.samples
            rate = sound.rate
            loop_start = sound.one_shot_samples if sound.repeat_samples else 0
            loop_length = sound.repeat_samples
        elif source.period:
            body = data
            rate = (NTSC_PAULA_CLOCK_HZ + source.period // 2) // source.period
            loop_start = 0
            loop_length = len(data) if source.looped else 0
        else:
            raise FormatError(f"{source.name} is neither an 8SVX sound nor a known bare one")
        entries.append((len(samples), len(body), rate, loop_start, loop_length))
        samples.extend(body)
    base = image.append("sound_samples", bytes(samples), len(samples))
    layout.sounds = [
        SoundEntry(Span(base + offset, count), rate, loop_start, loop_length)
        for offset, count, rate, loop_start, loop_length in entries
    ]


def pack_title_animation(image: Image, game: Game, layout: Layout) -> None:
    frames = images.parse_dif(game.read(TITLE_ANIMATION))
    masks = bytearray()
    runs: list[bytes] = []
    frame_records: list[bytes] = []
    for frame in frames:
        pixel_runs = images.dif_pixel_runs(frame)
        words = sum(len(run_words) for _, run_words in frame.runs)
        frame_records.append(XOR_FRAME.pack(len(runs), len(pixel_runs), words))
        for run in pixel_runs:
            runs.append(XOR_RUN.pack(run.offset, len(masks), len(run.masks)))
            masks.extend(run.masks)
    steps = images.parse_dif_sequence(game.executable.read(TITLE_PLAY_LIST, 0x100))
    if any(not 0 < frame <= len(frames) for frame, _ in steps):
        raise FormatError("the play list names a frame PRESENT.DIF does not have")
    step_records = [ANIMATION_STEP.pack(frame - 1, delay) for frame, delay in steps]
    layout.animation = {
        "masks": Span(image.append("animation_masks", bytes(masks), len(masks)), len(masks)),
        "runs": Span(image.append("animation_runs", concatenated(runs), len(runs)), len(runs)),
        "frames": Span(image.append("animation_frames", concatenated(frame_records), len(frames)), len(frames)),
        "steps": Span(image.append("animation_steps", concatenated(step_records), len(steps)), len(steps)),
    }


def pack_title_music(image: Image, game: Game, layout: Layout) -> None:
    music = audio.MusicFile.from_data(game.read(TITLE_MUSIC))
    samples = [
        MODULE_SAMPLE.pack(
            sample.offset, sample.size, sample.length, sample.loop_start, sample.loop_length, sample.volume
        )
        for sample in music.samples
    ]
    notes = [MODULE_NOTE.pack(note.period, note.sample, note.effect, note.parameter) for note in music.notes]
    data = music.sample_data
    layout.music = {
        "sample_data": Span(image.append("music_sample_data", data, len(data)), len(data)),
        "samples": Span(image.append("music_samples", concatenated(samples), len(samples)), len(samples)),
        "positions": Span(image.append("music_positions", music.positions, len(music.positions)), len(music.positions)),
        "notes": Span(image.append("music_notes", concatenated(notes), len(notes)), len(notes)),
    }
    layout.music_timer = music.timer


def pack_world(image: Image, game: Game, layout: Layout) -> None:
    cells = world.decode_road_map(game.read(ROAD_MAP))
    layout.road_map = Span(image.append("road_map", cells, len(cells)), len(cells))
    records: list[bytes] = []
    for objects in world.decode_object_placement(game.read(SCENERY)):
        layout.scenery_ranges.append((len(records), len(objects)))
        records.extend(PLACED_OBJECT.pack(item.x, item.y, item.z, item.type, item.extra) for item in objects)
    layout.scenery_objects = Span(image.append("scenery_objects", concatenated(records), len(records)), len(records))
    points: list[bytes] = []
    for place in ROAD_SHAPES:
        outline = world.decode_road_shape(bytes(game.executable.hunks[place.hunk].data[place.offset :]))
        layout.road_shape_ranges.append((len(points), len(outline)))
        points.extend(SHAPE_POINT.pack(point.x, point.y) for point in outline)
    layout.road_shape_points = Span(image.append("road_shape_points", concatenated(points), len(points)), len(points))


def build_image(root: Path) -> tuple[Image, Layout]:
    game = Game(root)
    image = Image()
    layout = Layout()
    headers = pack_pictures(image, game, layout)
    pack_palettes(image, game, headers, layout)
    pack_banks(image, game, layout)
    pack_fonts(image, game, layout)
    pack_sounds(image, game, layout)
    pack_title_animation(image, game, layout)
    pack_title_music(image, game, layout)
    pack_world(image, game, layout)
    return image, layout


def view(type_name: str, span: Span) -> str:
    return f"TableView<{type_name}>(base + {span.offset:#x}, {span.count})"


HEADER_TEMPLATE: str = """// Generated by scripts/pack_assets.py; do not edit by hand.
//
// The asset image in the "assets" partition: its size and SHA-256 for the boot-time check
// (src/target/flash/asset_check.hpp), where each section lies, and FlashAssets(base), the
// engine's view of the image at `base`, every span a constant offset from it.

#pragma once

#include <cstddef>
#include <cstdint>

#include "core/engine_assets.hpp"
#include "target/flash/asset_image.hpp"
#include "target/flash/digest.hpp"

namespace hp2::flash::asset_layout {{

struct Section {{
  std::size_t offset;
  std::size_t count;
}};

inline constexpr std::size_t kImageSize = {args.image_size:#x};
inline constexpr Digest kImageDigest =
    Sha256FromHex("{args.digest}");

{args.sections}

// Valid only once the image verified, or over a copy of assets.bin.
[[nodiscard]] inline EngineAssets FlashAssets(std::uintptr_t base) {{
  return EngineAssets{{
      .pictures = {{{{
{args.pictures}
      }}}},
      .palettes = {{{{
{args.palettes}
      }}}},
      .banks = {{{{
{args.banks}
      }}}},
      .fonts = {{{{
{args.fonts}
      }}}},
      .sounds = {{{{
{args.sounds}
      }}}},
      .title_animation = {{.masks = {args.animation_masks},
                          .runs = {args.animation_runs},
                          .frames = {args.animation_frames},
                          .steps = {args.animation_steps}}},
      .title_music = {{.sample_data = {args.music_sample_data},
                      .samples = {args.music_samples},
                      .positions = {args.music_positions},
                      .notes = {args.music_notes},
                      .timer = {args.music_timer:#x}}},
      .road_map = {{.cells = {args.road_map}}},
      .scenery = {{.objects = {args.scenery_objects},
                  .cell_types = {{{{
{args.scenery_ranges}
                  }}}}}},
      .road_shapes = {{.points = {args.road_shape_points},
                      .cell_types = {{{{
{args.road_shape_ranges}
                      }}}}}},
  }};
}}

}}  // namespace hp2::flash::asset_layout
"""
SECTION_TEMPLATE: str = "inline constexpr Section k{identifier}{{.offset = {offset:#x}, .count = {count}}};"
PICTURE_TEMPLATE: str = "          {{.width = {width}, .height = {height}, .pixels = {pixels}}},"
PALETTE_TEMPLATE: str = "          {segments},"
BANK_TEMPLATE: str = """          {{.pixels = {pixels},
           .sprites = {sprites}}},"""
FONT_TEMPLATE: str = "          {{.pixels = {pixels}}},"
SOUND_TEMPLATE: str = """          {{.samples = {samples},
           .rate_hz = {rate}, .loop_start = {loop_start}, .loop_length = {loop_length}}},"""
SCENERY_RANGE_TEMPLATE: str = "                      {{.first = {first}, .count = {count}}},"
SHAPE_RANGE_TEMPLATE: str = "                          {{.first = {first}, .count = {count}}},"


@dataclass(frozen=True)
class HeaderArgs:
    """What HEADER_TEMPLATE is filled with; views are TableView expressions."""

    image_size: int
    digest: str
    sections: str
    pictures: str
    palettes: str
    banks: str
    fonts: str
    sounds: str
    animation_masks: str
    animation_runs: str
    animation_frames: str
    animation_steps: str
    music_sample_data: str
    music_samples: str
    music_positions: str
    music_notes: str
    music_timer: int
    road_map: str
    scenery_objects: str
    scenery_ranges: str
    road_shape_points: str
    road_shape_ranges: str


def render_sections(image: Image) -> str:
    return "\n".join(
        SECTION_TEMPLATE.format(
            identifier="".join(piece.capitalize() for piece in section.name.split("_")),
            offset=section.offset,
            count=section.count,
        )
        for section in image.sections
    )


def render_pictures(layout: Layout) -> str:
    return "\n".join(
        PICTURE_TEMPLATE.format(width=SCREEN_WIDTH, height=SCREEN_HEIGHT, pixels=view("std::uint8_t", span))
        for span in layout.pictures
    )


def render_palettes(layout: Layout) -> str:
    return "\n".join(PALETTE_TEMPLATE.format(segments=view("PaletteSegment", span)) for span in layout.palettes)


def render_banks(layout: Layout) -> str:
    return "\n".join(
        BANK_TEMPLATE.format(pixels=view("std::uint8_t", bank.pixels), sprites=view("SpriteRange", bank.sprites))
        for bank in layout.banks
    )


def render_fonts(layout: Layout) -> str:
    return "\n".join(FONT_TEMPLATE.format(pixels=view("std::uint8_t", span)) for span in layout.fonts)


def render_sounds(layout: Layout) -> str:
    return "\n".join(
        SOUND_TEMPLATE.format(
            samples=view("std::int8_t", sound.samples),
            rate=sound.rate,
            loop_start=sound.loop_start,
            loop_length=sound.loop_length,
        )
        for sound in layout.sounds
    )


def render_ranges(template: str, ranges: list[tuple[int, int]]) -> str:
    return "\n".join(template.format(first=first, count=count) for first, count in ranges)


def render_header(image: Image, layout: Layout) -> str:
    return HEADER_TEMPLATE.format(
        args=HeaderArgs(
            image_size=len(image.data),
            digest=hashlib.sha256(image.data).hexdigest(),
            sections=render_sections(image),
            pictures=render_pictures(layout),
            palettes=render_palettes(layout),
            banks=render_banks(layout),
            fonts=render_fonts(layout),
            sounds=render_sounds(layout),
            animation_masks=view("std::uint8_t", layout.animation["masks"]),
            animation_runs=view("XorRun", layout.animation["runs"]),
            animation_frames=view("XorFrame", layout.animation["frames"]),
            animation_steps=view("AnimationStep", layout.animation["steps"]),
            music_sample_data=view("std::int8_t", layout.music["sample_data"]),
            music_samples=view("ModuleSample", layout.music["samples"]),
            music_positions=view("std::uint8_t", layout.music["positions"]),
            music_notes=view("ModuleNote", layout.music["notes"]),
            music_timer=layout.music_timer,
            road_map=view("std::uint8_t", layout.road_map),
            scenery_objects=view("PlacedObject", layout.scenery_objects),
            scenery_ranges=render_ranges(SCENERY_RANGE_TEMPLATE, layout.scenery_ranges),
            road_shape_points=view("ShapePoint", layout.road_shape_points),
            road_shape_ranges=render_ranges(SHAPE_RANGE_TEMPLATE, layout.road_shape_ranges),
        )
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME, help="the extracted game: hp.prg and DISK2_2/")
    parser.add_argument("--partitions", type=Path, required=True, help="the board's partitions.json")
    parser.add_argument("--out-dir", type=Path, required=True, help="where assets.bin and assets.uf2 go")
    parser.add_argument("--layout-header", type=Path, help="C++ layout header to write")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

    partition: Partition = find_partition(load_partitions(args.partitions), "assets")
    image, layout = build_image(args.game)
    if len(image.data) > partition.size:
        raise FormatError(f"assets: {len(image.data):#x} bytes do not fit the partition of {partition.size:#x}")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "assets.bin").write_bytes(image.data)
    (args.out_dir / "assets.uf2").write_bytes(
        UF2Packer(data=bytes(image.data), start_address=partition.address).to_uf2()
    )
    for section in image.sections:
        LOGGER.info("%-20s offset %#8x count %7d", section.name, section.offset, section.count)
    LOGGER.info("assets.bin %d bytes, assets.uf2 at %#010x", len(image.data), partition.address)
    if args.layout_header is not None:
        args.layout_header.write_text(render_header(image, layout))
        LOGGER.info("wrote %s", args.layout_header)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
