"""Pack the decoded game assets into the firmware's flash partition image.

    pack_assets.py --partitions src/boards/<board>/partitions.json --out-dir build/assets \\
        [--game "assets/hp2/Highway Patrol II"] [--layout-header src/target/flash/asset_layout.hpp]

Writes assets.bin, the image laid out as the engine's types are in memory (src/core/*.hpp; the
record layouts are the contract src/target/flash/asset_image.hpp checks), assets.uf2 targeting the
"assets" partition, and the layout header: the image size and SHA-256 the firmware checks before
using the image, AssetImage, the image as a structure with one member per asset, and
FlashAssets(image), the engine's EngineAssets over it. Tables are 4-byte aligned. The host test
compares the image with what the C++ asset manager decodes from the disk, so the two decoders
cannot drift apart unnoticed.

The contents follow EngineAssets (src/core/engine_assets.hpp), in the order of its enums; an
asset's name here is the name of its member in AssetImage.
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
TABLE_ALIGNMENT = 4
SCREEN_WIDTH = 320
SCREEN_HEIGHT = 200
COLOR_REGISTERS = 16
NTSC_PAULA_CLOCK_HZ = 3_579_545
AMIGA_FORMAT = 0
ATARI_ST_FORMAT = 1


@dataclass(frozen=True)
class FileSource:
    """An asset read from one game file."""

    name: str
    file: str


# In EnginePicture order.
PICTURES = (
    FileSource("logo", "LOGO.CPV"),
    FileSource("title", "PRESENT.CPV"),
    FileSource("office", "BUREAU.CPV"),
    FileSource("station", "STATION.CPV"),
    FileSource("spin_out", "PAGE_F1.CPV"),
    FileSource("tires_gone", "PAGE_F2.CPV"),
    FileSource("out_of_fuel", "PAGE_F3.CPV"),
    FileSource("overheated", "PAGE_F4.CPV"),
    FileSource("wrecked", "PAGE_F5.CPV"),
    FileSource("arrest", "PAGE_F6.CPV"),
)


@dataclass(frozen=True)
class PaletteSource:
    """A picture's header palette (`picture`, an index into PICTURES) or a list in hp.prg."""

    name: str
    picture: int | None = None
    place: HunkOffset | None = None
    color_format: int = AMIGA_FORMAT


# In EnginePalette order.
PALETTES = (
    PaletteSource("logo", picture=0),
    PaletteSource("station", picture=3),
    PaletteSource("spin_out", picture=4),
    PaletteSource("tires_gone", picture=5),
    PaletteSource("out_of_fuel", picture=6),
    PaletteSource("overheated", picture=7),
    PaletteSource("wrecked", picture=8),
    PaletteSource("arrest", picture=9),
    PaletteSource("title", place=HunkOffset(1, 0x28E0), color_format=ATARI_ST_FORMAT),  # titlePalette
    PaletteSource("office", place=HunkOffset(0, 0xCA94)),  # officePalette
    PaletteSource("office_dim", place=HunkOffset(0, 0xCA6E)),  # officePaletteDim
    PaletteSource("score", place=HunkOffset(0, 0x7766)),  # scorePalette
    PaletteSource("view", place=HunkOffset(0, 0xA520)),  # viewPalette
    PaletteSource("view_red", place=HunkOffset(0, 0xA734)),  # viewPaletteRed
)

# In EngineBank order.
BANKS = (
    FileSource("names", "NAME.IMG"),
    FileSource("office", "BUREAU.IMG"),
    FileSource("station", "STATION.IMG"),
    FileSource("score", "GAME_SCO.IMG"),
    FileSource("gun", "BALLE.IMG"),
    FileSource("cactus", "CACTUS.IMG"),
    FileSource("bush", "BUISSON.IMG"),
    FileSource("stones", "CAILLOUX.IMG"),
    FileSource("backdrop", "DEC_FOND.IMG"),
    FileSource("cockpit", "DES_TABB.IMG"),
    FileSource("sign_pole", "PAN_POT.IMG"),
    FileSource("sign_left", "PAN_GAU.IMG"),
    FileSource("sign_junction", "PAN_CRO.IMG"),
    FileSource("sign_right", "PAN_DRO.IMG"),
    FileSource("sign_back", "PAN_ARR.IMG"),
    FileSource("sign_edge", "PAN_PRO.IMG"),
    FileSource("station_arrow_left", "PST_FLG.IMG"),
    FileSource("station_arrow_right", "PST_FLD.IMG"),
    FileSource("station_edge", "PST_PRO.IMG"),
    FileSource("station_board", "PST_STA.IMG"),
    FileSource("car0", "VOITURE0.IMG"),
    FileSource("car1", "VOITURE1.IMG"),
    FileSource("car2", "VOITURE2.IMG"),
    FileSource("car3", "VOITURE3.IMG"),
    FileSource("car4", "VOITURE4.IMG"),
    FileSource("car5", "VOITURE5.IMG"),
    FileSource("car6", "VOITURE6.IMG"),
)

# In EngineFont order.
FONTS = (
    FileSource("lettre1", "LETTRE1.BIN"),
    FileSource("lettre2", "LETTRE2.BIN"),
)


@dataclass(frozen=True)
class SoundSource:
    """An 8SVX sound (period 0) or a bare one played at its period on the NTSC clock."""

    name: str
    file: str
    period: int = 0
    looped: bool = False


# In EngineSound order (src/host/asset_manager.cpp, kSoundSources).
SOUNDS = (
    SoundSource("engine", "MOTEUR.SND"),
    SoundSource("siren", "SIRENE.SND", period=864, looped=True),
    SoundSource("shot", "TIR.SND", period=427),
    SoundSource("skid", "DERAP.SND"),
    SoundSource("crash", "CHOC.SND", period=640),
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
RGB = struct.Struct("<3B")
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
class Table:
    """A member of AssetImage: `count` records of the C++ type `record`, `offset` bytes into the
    image. `path` names the member from the image ("banks.office.pixels")."""

    path: str
    record: str
    offset: int
    count: int

    @property
    def name(self) -> str:
        return self.path.rpartition(".")[2]


NO_TABLE = Table(path="", record="", offset=0, count=0)


@dataclass
class Image:
    """The image under construction: tables appended 4-byte aligned."""

    data: bytearray = field(default_factory=bytearray)
    tables: list[Table] = field(default_factory=list)

    def append(self, path: str, record: str, payload: bytes, count: int) -> Table:
        self.pad()
        table = Table(path=path, record=record, offset=len(self.data), count=count)
        self.tables.append(table)
        self.data.extend(payload)
        return table

    def pad(self) -> None:
        self.data.extend(bytes(-len(self.data) % TABLE_ALIGNMENT))


@dataclass(frozen=True)
class SoundEntry:
    samples: Table
    rate: int
    loop_start: int
    loop_length: int


@dataclass(frozen=True)
class BankEntry:
    name: str
    pixels: Table
    sprites: Table


@dataclass
class Layout:
    """The tables of the image, grouped as EngineAssets groups its views."""

    pictures: list[Table] = field(default_factory=list)
    palettes: list[Table] = field(default_factory=list)
    banks: list[BankEntry] = field(default_factory=list)
    fonts: list[Table] = field(default_factory=list)
    sounds: list[SoundEntry] = field(default_factory=list)
    animation: dict[str, Table] = field(default_factory=dict)
    music: dict[str, Table] = field(default_factory=dict)
    music_timer: int = 0
    road_map: Table = NO_TABLE
    scenery_objects: Table = NO_TABLE
    scenery_ranges: list[tuple[int, int]] = field(default_factory=list)
    road_shape_points: Table = NO_TABLE
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
    headers: list[tuple[int, ...]] = []
    for source in PICTURES:
        picture = images.decode_cpv(game.read(source.file))
        pixels = picture.indices().tobytes()
        if len(pixels) != SCREEN_WIDTH * SCREEN_HEIGHT:
            raise FormatError(f"{source.file} is not a {SCREEN_WIDTH} x {SCREEN_HEIGHT} picture")
        layout.pictures.append(image.append(f"pictures.{source.name}", "std::uint8_t", pixels, len(pixels)))
        headers.append(picture.palette_st)
    return headers


def palette_colors(words: Sequence[int], color_format: int) -> list[int]:
    """The color words decoded to RGB channels, COLOR_REGISTERS colors, the missing ones black."""
    convert = images.st_to_rgb if color_format == ATARI_ST_FORMAT else images.amiga_to_rgb
    channels = [channel for word in words for channel in convert(word)]
    return channels + [0] * (COLOR_REGISTERS * 3 - len(channels))


def palette_sets(game: Game, source: PaletteSource, headers: list[tuple[int, ...]]) -> list[int]:
    """The palette's channels: a picture's colors, or one set of COLOR_REGISTERS colors per segment of a
    list, each the colors in effect from its segment on."""
    if source.picture is not None:
        return palette_colors(headers[source.picture], ATARI_ST_FORMAT)
    assert source.place is not None
    channels: list[int] = []
    current = [0] * (COLOR_REGISTERS * 3)
    for segment in images.parse_palette_list(game.executable.read(source.place, 0x400)):
        if segment.first + len(segment.colors) > COLOR_REGISTERS:
            raise FormatError(f"palette list at {source.place} writes past register {COLOR_REGISTERS - 1}")
        written = len(segment.colors) * 3
        colors = palette_colors(segment.colors, source.color_format)
        current[segment.first * 3 : segment.first * 3 + written] = colors[:written]
        channels.extend(current)
    return channels


def pack_palettes(image: Image, game: Game, headers: list[tuple[int, ...]], layout: Layout) -> None:
    for source in PALETTES:
        channels = palette_sets(game, source, headers)
        layout.palettes.append(
            image.append(f"palettes.{source.name}", "Rgb", bytes(channels), len(channels) // RGB.size)
        )


def pack_banks(image: Image, game: Game, layout: Layout) -> None:
    """Every bank's images as color indices, its sprites as ranges of them."""
    for source in BANKS:
        pixels = bytearray()
        sprites: list[bytes] = []
        for bob in images.parse_bob_bank(game.read(source.file)):
            sprites.append(SPRITE_RANGE.pack(len(pixels), bob.width, bob.height, bob.origin_x, bob.origin_y))
            pixels.extend(bob.indices().tobytes())
        path = f"banks.{source.name}"
        layout.banks.append(
            BankEntry(
                name=source.name,
                pixels=image.append(f"{path}.pixels", "std::uint8_t", bytes(pixels), len(pixels)),
                sprites=image.append(f"{path}.sprites", "SpriteRange", concatenated(sprites), len(sprites)),
            )
        )


def pack_fonts(image: Image, game: Game, layout: Layout) -> None:
    for source in FONTS:
        glyphs = concatenated(glyph.tobytes() for glyph in images.decode_font(game.read(source.file)))
        layout.fonts.append(image.append(f"fonts.{source.name}", "std::uint8_t", glyphs, len(glyphs)))


def pack_sounds(image: Image, game: Game, layout: Layout) -> None:
    for source in SOUNDS:
        data = game.read(source.file)
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
            raise FormatError(f"{source.file} is neither an 8SVX sound nor a known bare one")
        samples = image.append(f"sounds.{source.name}", "std::int8_t", bytes(body), len(body))
        layout.sounds.append(SoundEntry(samples, rate, loop_start, loop_length))


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
        "masks": image.append("title_animation.masks", "std::uint8_t", bytes(masks), len(masks)),
        "runs": image.append("title_animation.runs", "XorRun", concatenated(runs), len(runs)),
        "frames": image.append("title_animation.frames", "XorFrame", concatenated(frame_records), len(frames)),
        "steps": image.append("title_animation.steps", "AnimationStep", concatenated(step_records), len(steps)),
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
        "sample_data": image.append("title_music.sample_data", "std::int8_t", data, len(data)),
        "samples": image.append("title_music.samples", "ModuleSample", concatenated(samples), len(samples)),
        "positions": image.append("title_music.positions", "std::uint8_t", music.positions, len(music.positions)),
        "notes": image.append("title_music.notes", "ModuleNote", concatenated(notes), len(notes)),
    }
    layout.music_timer = music.timer


def pack_world(image: Image, game: Game, layout: Layout) -> None:
    cells = world.decode_road_map(game.read(ROAD_MAP))
    layout.road_map = image.append("road_map", "std::uint8_t", cells, len(cells))
    records: list[bytes] = []
    for objects in world.decode_object_placement(game.read(SCENERY)):
        layout.scenery_ranges.append((len(records), len(objects)))
        records.extend(PLACED_OBJECT.pack(item.x, item.y, item.z, item.type, item.extra) for item in objects)
    layout.scenery_objects = image.append("scenery_objects", "PlacedObject", concatenated(records), len(records))
    points: list[bytes] = []
    for place in ROAD_SHAPES:
        outline = world.decode_road_shape(bytes(game.executable.hunks[place.hunk].data[place.offset :]))
        layout.road_shape_ranges.append((len(points), len(outline)))
        points.extend(SHAPE_POINT.pack(point.x, point.y) for point in outline)
    layout.road_shape_points = image.append("road_shape_points", "ShapePoint", concatenated(points), len(points))


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
    # AssetImage's size is a multiple of its alignment.
    image.pad()
    return image, layout


def view(table: Table) -> str:
    """The C++ expression of `table` in FlashAssets."""
    return f"image.{table.path}"


HEADER_TEMPLATE: str = """// Generated by scripts/pack_assets.py; do not edit by hand.
//
// The asset image in the "assets" partition: its size and SHA-256 for the boot-time check
// (src/target/flash/asset_check.hpp), AssetImage, the image as a structure, and
// FlashAssets(image), the engine's views of it.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/engine_assets.hpp"
#include "target/flash/asset_image.hpp"
#include "target/flash/digest.hpp"

namespace hp2::flash::asset_layout {{

inline constexpr std::size_t kImageSize = {args.image_size:#x};
inline constexpr Digest kImageDigest =
    Sha256FromHex("{args.digest}");

// The image: every asset a member, each table {args.alignment}-byte aligned as the packer lays them.
struct AssetImage {{
  struct Pictures {{
{args.picture_tables}
  }};

  struct Palettes {{
{args.palette_tables}
  }};

  // A sprite bank: its pixels, and its sprites as ranges of them.
  template <std::size_t PixelCount, std::size_t SpriteCount>
  struct Bank {{
    alignas({args.alignment}) std::array<std::uint8_t, PixelCount> pixels;
    alignas({args.alignment}) std::array<SpriteRange, SpriteCount> sprites;
  }};

  struct Banks {{
{args.bank_members}
  }};

  struct Fonts {{
{args.font_tables}
  }};

  struct Sounds {{
{args.sound_tables}
  }};

  struct TitleAnimation {{
{args.animation_tables}
  }};

  struct TitleMusic {{
{args.music_tables}
  }};

  Pictures pictures;
  Palettes palettes;
  Banks banks;
  Fonts fonts;
  Sounds sounds;
  TitleAnimation title_animation;
  TitleMusic title_music;
{args.world_tables}
}};

static_assert(sizeof(AssetImage) == kImageSize);
{args.offsets}

// The engine's views of `image`: the partition once it verified, or a copy of assets.bin.
[[nodiscard]] constexpr EngineAssets FlashAssets(const AssetImage& image) {{
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
TABLE_TEMPLATE: str = "{indent}alignas({alignment}) std::array<{record}, {count}> {name};"
BANK_MEMBER_TEMPLATE: str = "    Bank<{pixels}, {sprites}> {name};"
OFFSET_TEMPLATE: str = "static_assert(offsetof(AssetImage, {name}) == {offset:#x});"
PICTURE_TEMPLATE: str = "          {{.width = {width}, .height = {height}, .pixels = {pixels}}},"
PALETTE_TEMPLATE: str = "          {colors},"
BANK_TEMPLATE: str = "          {{.pixels = {pixels}, .sprites = {sprites}}},"
FONT_TEMPLATE: str = "          {{.pixels = {pixels}}},"
SOUND_TEMPLATE: str = (
    "          {{.samples = {samples}, .rate_hz = {rate}, .loop_start = {loop_start}, .loop_length = {loop_length}}},"
)
SCENERY_RANGE_TEMPLATE: str = "                      {{.first = {first}, .count = {count}}},"
SHAPE_RANGE_TEMPLATE: str = "                          {{.first = {first}, .count = {count}}},"


@dataclass(frozen=True)
class HeaderArgs:
    """What HEADER_TEMPLATE is filled with: the members of AssetImage, then the views over them."""

    image_size: int
    digest: str
    alignment: int
    picture_tables: str
    palette_tables: str
    bank_members: str
    font_tables: str
    sound_tables: str
    animation_tables: str
    music_tables: str
    world_tables: str
    offsets: str
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


def render_tables(tables: Iterable[Table], indent: str = "    ") -> str:
    """The tables as members of AssetImage or of one of its structures."""
    return "\n".join(
        TABLE_TEMPLATE.format(
            indent=indent, alignment=TABLE_ALIGNMENT, record=table.record, count=table.count, name=table.name
        )
        for table in tables
    )


def render_bank_members(layout: Layout) -> str:
    return "\n".join(
        BANK_MEMBER_TEMPLATE.format(pixels=bank.pixels.count, sprites=bank.sprites.count, name=bank.name)
        for bank in layout.banks
    )


def render_offsets(layout: Layout) -> str:
    """Where each member of AssetImage lies: the offset of its first table."""
    members = {
        "pictures": layout.pictures[0],
        "palettes": layout.palettes[0],
        "banks": layout.banks[0].pixels,
        "fonts": layout.fonts[0],
        "sounds": layout.sounds[0].samples,
        "title_animation": layout.animation["masks"],
        "title_music": layout.music["sample_data"],
        "road_map": layout.road_map,
        "scenery_objects": layout.scenery_objects,
        "road_shape_points": layout.road_shape_points,
    }
    return "\n".join(OFFSET_TEMPLATE.format(name=name, offset=table.offset) for name, table in members.items())


def render_pictures(layout: Layout) -> str:
    return "\n".join(
        PICTURE_TEMPLATE.format(width=SCREEN_WIDTH, height=SCREEN_HEIGHT, pixels=view(table))
        for table in layout.pictures
    )


def render_palettes(layout: Layout) -> str:
    return "\n".join(PALETTE_TEMPLATE.format(colors=view(table)) for table in layout.palettes)


def render_banks(layout: Layout) -> str:
    return "\n".join(
        BANK_TEMPLATE.format(pixels=view(bank.pixels), sprites=view(bank.sprites)) for bank in layout.banks
    )


def render_fonts(layout: Layout) -> str:
    return "\n".join(FONT_TEMPLATE.format(pixels=view(table)) for table in layout.fonts)


def render_sounds(layout: Layout) -> str:
    return "\n".join(
        SOUND_TEMPLATE.format(
            samples=view(sound.samples),
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
            alignment=TABLE_ALIGNMENT,
            picture_tables=render_tables(layout.pictures),
            palette_tables=render_tables(layout.palettes),
            bank_members=render_bank_members(layout),
            font_tables=render_tables(layout.fonts),
            sound_tables=render_tables(sound.samples for sound in layout.sounds),
            animation_tables=render_tables(layout.animation.values()),
            music_tables=render_tables(layout.music.values()),
            world_tables=render_tables(
                (layout.road_map, layout.scenery_objects, layout.road_shape_points), indent="  "
            ),
            offsets=render_offsets(layout),
            pictures=render_pictures(layout),
            palettes=render_palettes(layout),
            banks=render_banks(layout),
            fonts=render_fonts(layout),
            sounds=render_sounds(layout),
            animation_masks=view(layout.animation["masks"]),
            animation_runs=view(layout.animation["runs"]),
            animation_frames=view(layout.animation["frames"]),
            animation_steps=view(layout.animation["steps"]),
            music_sample_data=view(layout.music["sample_data"]),
            music_samples=view(layout.music["samples"]),
            music_positions=view(layout.music["positions"]),
            music_notes=view(layout.music["notes"]),
            music_timer=layout.music_timer,
            road_map=view(layout.road_map),
            scenery_objects=view(layout.scenery_objects),
            scenery_ranges=render_ranges(SCENERY_RANGE_TEMPLATE, layout.scenery_ranges),
            road_shape_points=view(layout.road_shape_points),
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
    for table in image.tables:
        LOGGER.info("%-36s offset %#8x count %7d", table.path, table.offset, table.count)
    LOGGER.info("assets.bin %d bytes, assets.uf2 at %#010x", len(image.data), partition.address)
    if args.layout_header is not None:
        args.layout_header.write_text(render_header(image, layout))
        LOGGER.info("wrote %s", args.layout_header)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
