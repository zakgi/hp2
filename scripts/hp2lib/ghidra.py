"""GhidraMCP REST client bound to the hp2 project.

Several Ghidra instances run side by side and each takes the next free port
from 8089 up, so the client finds hp2's port by asking every candidate for
its project name. HP2_GHIDRA_PORT skips the probe but is still checked.
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

PROJECT = "hp2"
PROGRAM = "hp.prg"
PORTS = range(8089, 8100)


class GhidraError(RuntimeError):
    pass


def _request(port: int, path: str, query: dict[str, Any] | None, body: Any | None, timeout: float) -> Any:
    url = f"http://127.0.0.1:{port}{path}"
    if query:
        url += "?" + urllib.parse.urlencode(query)
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        text = resp.read().decode("utf-8", "replace")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def _project_of(port: int) -> str | None:
    try:
        info = _request(port, "/project/info", None, None, 2.0)
    except (urllib.error.URLError, OSError):
        return None
    return info.get("project") if isinstance(info, dict) else None


def find_port() -> int:
    env = os.environ.get("HP2_GHIDRA_PORT")
    candidates = [int(env)] if env else list(PORTS)
    for port in candidates:
        if _project_of(port) == PROJECT:
            return port
    raise GhidraError(f"no Ghidra instance with project {PROJECT!r} on ports {candidates}")


class Ghidra:
    def __init__(self, port: int | None = None, program: str = PROGRAM, timeout: float = 120.0) -> None:
        self.port = port if port is not None else find_port()
        if _project_of(self.port) != PROJECT:
            raise GhidraError(f"port {self.port} is not project {PROJECT!r}")
        self.program = program
        self.timeout = timeout

    def get(self, path: str, **query: Any) -> Any:
        query.setdefault("program", self.program)
        return _request(self.port, path, query, None, self.timeout)

    def post(self, path: str, body: dict[str, Any] | None = None, **query: Any) -> Any:
        query.setdefault("program", self.program)
        return _request(self.port, path, query, body or {}, self.timeout)

    def run_script(self, code: str, args: str = "") -> Any:
        return self.post("/run_script_inline", {"code": code, "args": args})

    def save(self) -> Any:
        return self.get("/save_program")
