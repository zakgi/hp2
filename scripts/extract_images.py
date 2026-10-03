"""Decode the Highway Patrol II pictures (DISK2_2/*.CPV, *.IMG, PRESENT.DIF, LETTRE*.BIN) to PNG.

usage: python3 scripts/extract_images.py [--data DIR] [--exe hp.prg] [--out DIR] [--no-frames]

Output under work/assets/png/:
  cpv/<NAME>.png                 full-screen pictures with the palette in their own header
  cpv/PRESENT_exe_palette.png    PRESENT.CPV with the split palette the title really uses (1:28e0)
  img/<NAME>_sheet.png           every image of a bob bank on one sheet (colour 0 transparent)
  img/<NAME>/<NN>.png            each image of the bank (1-based BlitBob index)
  dif/PRESENT_sNN_fMM.png        PRESENT.CPV after step NN of the play list (frame MM of PRESENT.DIF)
  dif/PRESENT.gif                the same steps as an animation (timing estimated)
  dif/TITLE_final.png            last DIF step + NAME.IMG bobs + the 8x8 block copy, as main builds it
  font/<NAME>.png                LETTRE1/2.BIN glyphs 0x20..0x7d, 16 per row

Palettes not stored in the file are read from hp.prg; see PALETTE_SOURCES and
work/analysis/frontend.md for the evidence.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image

from hp2lib.exe import DATA_DIR, DEFAULT_EXE, Executable, HunkOffset
from hp2lib.images import (
    PaletteSpec,
    blit_bob,
    contact_sheet,
    decode_cpv,
    decode_font,
    parse_bob_bank,
    parse_dif,
    parse_dif_sequence,
    parse_palette_list,
    play_dif,
    render,
    row_palettes,
    screen_to_indices,
)

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "work" / "assets" / "png"

# Palette lists in hp.prg (format of InstallPalette 1:19b4).
PAL_PRESENT = HunkOffset(1, 0x28E0)  # ST format, 2 segments split at row 36; title fade 0:abc2, DIF player
PAL_BUREAU = HunkOffset(0, 0xCA94)  # office screen, installed at 0:c4dc
PAL_GAME_SCO = HunkOffset(0, 0x7766)  # score screens of 0:6008 (segment 0 rows 0-167, text from 168)
PAL_QUESTION = HunkOffset(0, 0xC338)  # dead manual-lookup screen 0:bd4e (segment 1 rows 98-151 = tiles)
PAL_INGAME = HunkOffset(0, 0xA520)  # in-game split palette, installed at 0:b9a8
CAR_COLOURS = HunkOffset(0, 0xA6F4)  # 0:e5d0: 8-byte entries patched into colours 4-7 of segment 1
PLAYLIST_DIF = HunkOffset(1, 0x2870)  # {frame, delay} list read by 0:106e

CPU_HZ = 7_159_090  # NTSC 68000 clock, for the GIF timing estimate only


def palette_rows(exe: Executable, place: HunkOffset, st: bool = False, patch: bytes | None = None) -> list[list[int]]:
    """Per-row palettes of the list at ``place`` (optionally patched like 0:e5d0)."""
    raw = bytearray(exe.read(place, 0x400))
    if patch is not None:
        raw[0x26 + 0x0E : 0x26 + 0x0E + len(patch)] = patch
    return row_palettes(parse_palette_list(bytes(raw)), st=st)


def ingame_rows(exe: Executable, car_entry: int = 0) -> list[list[int]]:
    """In-game palette per row after 0:e5d0(D0=car_entry) (main calls it with 0 at 0:b990)."""
    return palette_rows(exe, PAL_INGAME, patch=exe.read(CAR_COLOURS + 8 * car_entry, 8))


def save(img: Image.Image, path: Path, written: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    written.append(path)


def find(data_dir: Path, name: str) -> Path:
    """Case-insensitive lookup (AmigaDOS names are case-insensitive; the disk has bureau.cpv)."""
    for p in data_dir.iterdir():
        if p.name.upper() == name.upper():
            return p
    raise FileNotFoundError(name)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, default=DATA_DIR)
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    ap.add_argument("--no-frames", action="store_true", help="skip the per-image PNGs of bob banks")
    ns = ap.parse_args()

    exe = Executable(ns.exe)
    out: Path = ns.out
    written: list[Path] = []
    notes: list[str] = []

    # ---- palettes from the executable
    present_rows = palette_rows(exe, PAL_PRESENT, st=True)
    game_rows = ingame_rows(exe)
    bureau = palette_rows(exe, PAL_BUREAU)[0]
    game_sco = palette_rows(exe, PAL_GAME_SCO)[0]
    question_rows = palette_rows(exe, PAL_QUESTION)

    # ---- .CPV
    cpvs = {}
    for path in sorted(p for p in ns.data.iterdir() if p.suffix.upper() == ".CPV"):
        name = path.stem.upper()
        pic = decode_cpv(path.read_bytes())
        cpvs[name] = pic
        save(render(pic.indices(), pic.palette), out / "cpv" / f"{name}.png", written)
        notes.append(f"{name}.CPV: {pic.consumed}/{path.stat().st_size} bytes read, header palette")
    present = cpvs["PRESENT"]
    save(render(present.indices(), present_rows), out / "cpv" / "PRESENT_exe_palette.png", written)
    if bureau != cpvs["BUREAU"].palette:
        notes.append("WARNING: 0:ca94 differs from bureau.cpv header palette")
    else:
        notes.append("0:ca94 == bureau.cpv header palette converted by 0:1006")
    if present_rows[199] != present.palette:
        notes.append("WARNING: 1:28e0 segment 2 differs from PRESENT.CPV header palette")
    else:
        notes.append("1:28e0 segment 2 == PRESENT.CPV header palette")

    # ---- PRESENT.DIF, replayed over PRESENT.CPV with the exe play list
    frames = parse_dif(find(ns.data, "PRESENT.DIF").read_bytes())
    sequence = parse_dif_sequence(exe.read(PLAYLIST_DIF, 0x80))
    base = render(present.indices(), present_rows)
    save(base, out / "dif" / "PRESENT_s00_base.png", written)
    gif_frames = [base]
    durations = [400]
    last_planes = present.planes
    for step, number, delay, planes in play_dif(present.planes, frames, sequence):
        img = render(screen_to_indices(planes), present_rows)
        save(img, out / "dif" / f"PRESENT_s{step:02d}_f{number:02d}.png", written)
        gif_frames.append(img)
        # 0:106e busy-waits two dbf loops of `delay` (about 10 cycles per pass) per step.
        durations.append(max(20, round(2 * (delay + 1) * 10 / CPU_HZ * 1000)))
        last_planes = planes
    gif = out / "dif" / "PRESENT.gif"
    gif_frames[0].save(gif, save_all=True, append_images=gif_frames[1:], duration=durations, loop=0)
    written.append(gif)
    notes.append(f"PRESENT.DIF: {len(frames)} frames, play list {len(sequence)} steps at {PLAYLIST_DIF}")

    # Title as main leaves it before WaitFire: NAME.IMG bobs 2 and 3, then the 8x8 copy at 0:ad92.
    name_bank = parse_bob_bank(find(ns.data, "NAME.IMG").read_bytes())
    title = screen_to_indices(last_planes).copy()
    blit_bob(title, name_bank[1], 10, 189)
    blit_bob(title, name_bank[2], 258, 143)
    title[192:200, 160:168] = title[192:200, 240:248]
    save(render(title, present_rows), out / "dif" / "TITLE_final.png", written)

    # ---- .IMG bob banks
    # name -> (palette or per-row palettes, first screen row, description)
    sources: dict[str, tuple[PaletteSpec, int, str]] = {
        "BUREAU": (bureau, 0, "exe 0:ca94 (office screen)"),
        "GAME_SCO": (game_sco, 0, "exe 0:7766 segment 0 (rows 0-167 of the score screen)"),
        "MODULE": (question_rows[100], 0, "exe 0:c35e (segment 1 of 0:c338, rows 98-151)"),
        "NAME": (present_rows[150], 0, "exe 1:28e0 segment 2 (= PRESENT.CPV header)"),
        "STATION": (cpvs["STATION"].palette, 0, "STATION.CPV header (drawn over it at 0:3608/0:3652)"),
        "DES_TABB": (game_rows[132], 0, "exe 0:a520 last segment 0:a6ce (rows 132-199)"),
        "DEC_FOND": (game_rows, 50, "exe 0:a520 per row from y=50 (0:df56 default y)"),
    }
    ingame = (game_rows[100], 0, "exe 0:a520 at row 100, colours 4-7 from 0:a6f4 entry 0")
    for path in sorted(p for p in ns.data.iterdir() if p.suffix.upper() == ".IMG"):
        name = path.stem.upper()
        palette, first_row, desc = sources.get(name, ingame)
        bank = parse_bob_bank(path.read_bytes())
        imgs = [render(b.indices(), palette, transparent_zero=True, first_row=first_row) for b in bank]
        cols = 1 if max(b.width for b in bank) >= 160 else (4 if max(b.width for b in bank) >= 64 else 8)
        save(contact_sheet(imgs, columns=cols), out / "img" / f"{name}_sheet.png", written)
        if not ns.no_frames:
            for b, im in zip(bank, imgs):
                save(im, out / "img" / name / f"{b.index:02d}.png", written)
        notes.append(f"{name}.IMG: {len(bank)} images, palette {desc}")

    # ---- fonts
    for fname in ("LETTRE1.BIN", "LETTRE2.BIN"):
        glyphs = decode_font(find(ns.data, fname).read_bytes())
        rows = (len(glyphs) + 15) // 16
        sheet = np.zeros((rows * 8, 16 * 8), dtype=np.uint8)
        for i, g in enumerate(glyphs):
            sheet[(i // 16) * 8 : (i // 16) * 8 + 8, (i % 16) * 8 : (i % 16) * 8 + 8] = g
        save(render(sheet, game_rows[0]), out / "font" / f"{fname.split('.')[0]}.png", written)
        notes.append(f"{fname}: {len(glyphs)} glyphs, palette exe 0:a520 segment 0")

    for n in notes:
        print(n)
    print(f"{len(written)} files written under {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
