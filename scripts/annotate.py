"""Apply a JSON annotation batch to hp.prg (see ghidra_scripts/ApplyAnnotations.java) and save.

usage: python3 scripts/annotate.py work/batches/01-system.json
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from hp2lib.ghidra import Ghidra

SCRIPT = Path(__file__).resolve().parent / "ghidra_scripts" / "ApplyAnnotations.java"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("batch", type=Path)
    parser.add_argument("--port", type=int)
    ns = parser.parse_args()

    batch_path = ns.batch.resolve()
    json.loads(batch_path.read_text())  # fail early on malformed JSON

    g = Ghidra(ns.port)
    result = g.run_script(SCRIPT.read_text(), str(batch_path))
    out = result.get("console_output", "") if isinstance(result, dict) else str(result)
    start = out.find("--- SCRIPT OUTPUT ---")
    text = out[start:] if start >= 0 else out
    keep = ("ERROR", "MISMATCH", "applied:", "Exception", "error:")
    # OSGi felixcache NoSuchFileException lines after each run are Ghidra noise.
    noise = ("T.java", "felix", "Bundle ")
    print("\n".join(line for line in text.splitlines() if line.startswith(keep) and not any(n in line for n in noise)))
    if isinstance(result, dict) and not result.get("success", True):
        return 1
    if "applied: errors=0" not in text or "mismatched=0" not in text:
        return 1
    # The script runner's own transaction can still be closing when the call returns.
    for _ in range(10):
        saved = g.save()
        if isinstance(saved, dict) and saved.get("success"):
            print("saved")
            return 0
        time.sleep(0.5)
    print(f"save failed: {saved}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
