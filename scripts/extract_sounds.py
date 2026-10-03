"""Convert the Highway Patrol II sound effects (DISK2_2/*.SND) to WAV.

usage: python3 scripts/extract_sounds.py [--clock pal|ntsc] [--loops N]
                                       [--engine-speeds 0,100,400] [--skip-iff-header]

Each .SND is raw signed 8-bit Amiga sample data (MOTEUR.SND and DERAP.SND are
IFF 8SVX files, but the game does not parse the IFF chunks).  The byte range,
Amiga period and loop mode written here are the values hp.prg uses:

  main 0:b9e8-0:ba6c   sample pointer/length table 0:139c-0:13c0
                           (pointer = chip buffer 1:2a38 + offset, length in words)
  0:1162             per-frame mixer: channel, period, volume and loop flag
                           passed to 0:1456 for every sound

The playback rate is clock / period, where clock is the Paula clock of the machine:
PAL 3546895 Hz (default, the game is a European release) or NTSC 3579545 Hz.
The engine period is 640 - player speed (0x280 - [1:3b50+8]); speed is 0..400
forward (0..100 in reverse), so it is rendered at the speeds given with
--engine-speeds (default: 0 = idle, period 640).

Output: work/assets/wav/<name>.wav, 8-bit unsigned mono PCM (sample values are
not scaled by the in-game volume).
"""

from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "assets" / "hp2" / "Highway Patrol II" / "DISK2_2"
OUT_DIR = ROOT / "work" / "assets" / "wav"

CLOCKS = {"pal": 3546895, "ntsc": 3579545}

IFF_HEADER = 0x68  # FORM/8SVX/VHDR/NAME/ANNO/BODY header size in MOTEUR.SND and DERAP.SND

# name, file on DISK2_2, byte offset, length in words (as the code passes it to
# AUDx.LEN), period (None = engine, depends on speed), loop, channel, volume, note
SOUNDS = [
    ("moteur", "MOTEUR.SND", 0x92, 0x115, None, True, 0, 0x40, "engine; restarted only when the period changes"),
    ("sirene", "SIRENE.SND", 0x0, 0x2E8B, 0x360, True, 1, 0x3F, "siren; looped while 0:1392 is set each frame"),
    ("tir", "TIR.SND", 0x0, 0xF8E, 0x1AB, False, 2, 0x3F, "gunshot (player or target car)"),
    (
        "derap",
        "DERAP.SND",
        0x0,
        0xE42,
        0x384,
        False,
        3,
        0x3F,
        "skid (vol 0x3f) / leaving the road (vol 0x20); IFF header is played too",
    ),
    ("choc", "CHOC.SND", 0x0, 0x7D5, 0x280, False, 2, 0x3F, "crash"),
]


def find_file(name: str) -> Path:
    """AmigaDOS names are case-insensitive (the exe asks for SIRENE.SND, the disk has Sirene.snd)."""
    for p in SRC_DIR.iterdir():
        if p.name.upper() == name.upper():
            return p
    raise FileNotFoundError(SRC_DIR / name)


def write_wav(path: Path, data: bytes, rate: int) -> None:
    pcm = bytes((b + 128) & 0xFF for b in data)  # signed 8-bit -> unsigned 8-bit
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(pcm)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--clock", choices=sorted(CLOCKS), default="pal")
    ap.add_argument("--loops", type=int, default=1, help="repetitions written for looping sounds")
    ap.add_argument(
        "--engine-speeds",
        default="0",
        help="comma-separated player speeds to render the engine at (period 640 - speed)",
    )
    ap.add_argument(
        "--skip-iff-header",
        action="store_true",
        help="drop the 0x68-byte IFF header that the game plays at the start of DERAP",
    )
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    ns = ap.parse_args()

    clock = CLOCKS[ns.clock]
    ns.out.mkdir(parents=True, exist_ok=True)
    speeds = [int(s) for s in ns.engine_speeds.split(",") if s.strip()]

    print(f"clock {ns.clock.upper()} {clock} Hz -> {ns.out}")
    for name, fname, off, words, period, loop, ch, vol, note in SOUNDS:
        raw = find_file(fname).read_bytes()
        start, end = off, off + words * 2
        if end > len(raw):
            print(f"warning: {fname} is {len(raw)} bytes, code plays up to {end:#x}", file=sys.stderr)
        if ns.skip_iff_header and raw[:4] == b"FORM" and start < IFF_HEADER:
            start = IFF_HEADER
        data = raw[start:end]
        reps = max(1, ns.loops) if loop else 1
        variants = (
            [(f"{name}" if len(speeds) == 1 and speeds[0] == 0 else f"{name}_speed{s:03d}", 0x280 - s) for s in speeds]
            if period is None
            else [(name, period)]
        )
        for out_name, per in variants:
            rate = round(clock / per)
            write_wav(ns.out / f"{out_name}.wav", data * reps, rate)
            print(
                f"  {out_name + '.wav':22s} {fname:11s} +{start:#06x} {len(data):6d} bytes  "
                f"period {per:4d} ({per:#05x}) -> {rate:5d} Hz  ch{ch} vol {vol:#04x} "
                f"{'loop' if loop else 'one-shot'}  {note}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
