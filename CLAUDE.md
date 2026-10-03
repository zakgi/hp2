# Working rules for this repo

- This is an independent project. Code, docs, comments and commit-ready files never mention other projects.
- No git writes (no add, commit, checkout, stash). The user commits.
- No file operations outside this repo, except scratch files in the session scratchpad.
- Ghidra annotations and scripts need no permission. Code changes need approval at every step: propose the exact change and wait for a yes, one file or a couple of closely related edits at a time.
- Python: `uv sync --extra dev` creates `.venv/` in the repo; run tools as `.venv/bin/python scripts/<tool>.py`. `scripts/hp2lib` is the package; no `sys.path` manipulation. ruff, black and mypy must pass on `scripts/`.
- Input is recorded as keystroke events (press and release), never as key levels.
- Refer to places in hp.prg as `hunk:offset` with a hex offset (`1:2870`), in code, comments, docs, tools and replies; never by Ghidra address. Only the Ghidra tooling converts (`hp2lib.exe`).
- Ghidra: project `hp2`, program `hp.prg`, reached through GhidraMCP on a port that changes with launch order (`scripts/hp2lib/ghidra.py` finds it). Annotation batches go through `scripts/annotate.py work/batches/<batch>.json` (one transaction, read-back, save).
- Edit files with the Edit and Write tools.
