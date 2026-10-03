"""Print the call graph of work/listing/hp.lst: per function its place, size, callers and callees."""

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

from hp2lib.exe import from_ghidra

LISTING = Path(sys.argv[1] if len(sys.argv) > 1 else "work/listing/hp.lst")

func_re = re.compile(r"^; ======== FUNCTION (\S+)")
ins_re = re.compile(r"^([0-9a-f]{8})  [0-9a-f]+\s+(\S+)\s+(.*?)\s*(?:;\s*->(.*))?$")

funcs: list[tuple[str, int]] = []
calls: dict[str, set[str]] = defaultdict(set)
size: dict[str, int] = defaultdict(int)
cur = None
for line in LISTING.read_text().splitlines():
    m = func_re.match(line)
    if m:
        cur = m.group(1)
        continue
    if line.startswith(";;;;;;;; BLOCK"):
        cur = None
        continue
    m = ins_re.match(line)
    if not m or cur is None:
        continue
    if not funcs or funcs[-1][0] != cur:
        funcs.append((cur, int(m.group(1), 16)))
    size[cur] += 1
    mnem, refs = m.group(2), m.group(4)
    if mnem in ("jsr", "bsr.w", "bsr.b", "bsr") and refs:
        calls[cur].add(refs.split(" ->")[0].strip())

callers: dict[str, set[str]] = defaultdict(set)
for f, cs in calls.items():
    for c in cs:
        callers[c].add(f)

for name, addr in funcs:
    print(f"{from_ghidra(addr)} {name} [{size[name]} ins]")
    if callers[name]:
        print("   <- " + " ".join(sorted(callers[name])))
    if calls[name]:
        print("   -> " + " ".join(sorted(calls[name])))
