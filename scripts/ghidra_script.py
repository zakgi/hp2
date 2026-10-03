"""Run a Java GhidraScript file against hp.prg through GhidraMCP's run_script_inline.

usage: python3 scripts/ghidra_script.py scripts/ghidra_scripts/DumpListing.java [args...]

Relative path arguments that look like files under the repo are passed as absolute
paths, because the script runs inside Ghidra's working directory.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from hp2lib.ghidra import Ghidra


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("script", type=Path)
    parser.add_argument("args", nargs="*")
    parser.add_argument("--port", type=int)
    ns = parser.parse_args()

    args = [str(Path(a).resolve()) if ("/" in a and not a.startswith("0x")) else a for a in ns.args]
    result = Ghidra(ns.port).run_script(ns.script.read_text(), " ".join(args))
    if isinstance(result, dict):
        out = result.get("console_output", "")
        start = out.find("--- SCRIPT OUTPUT ---")
        print(out[start:] if start >= 0 else out)
        return 0 if result.get("success", True) else 1
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
