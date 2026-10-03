"""Read hp.prg memory (relocated, as Ghidra loaded it) through GhidraMCP.

Offline tools use hp2lib.exe instead; this module is for checks against the live Ghidra
program, whose memory may carry patches the file does not.
"""

from __future__ import annotations

import struct

from .ghidra import Ghidra

CHUNK_BYTES = 4096


def read(ghidra: Ghidra, address: int, length: int) -> bytes:
    out = bytearray()
    while length > 0:
        count = min(length, CHUNK_BYTES)
        reply = ghidra.get("/read_memory", address=f"{address:08x}", length=count)
        data = reply.get("hex") if isinstance(reply, dict) else None
        if not isinstance(data, str):
            raise TypeError(f"unexpected read_memory reply: {str(reply)[:200]}")
        out += bytes.fromhex(data.replace(" ", ""))
        address += count
        length -= count
    return bytes(out)


def words(data: bytes, signed: bool = True) -> list[int]:
    count = len(data) // 2
    return list(struct.unpack(f">{count}{'h' if signed else 'H'}", data[: count * 2]))


def longs(data: bytes) -> list[int]:
    count = len(data) // 4
    return list(struct.unpack(f">{count}I", data[: count * 4]))
