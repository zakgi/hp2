"""Pack a flat binary into UF2 blocks for the RP2350 data family.

UF2 is the drag-and-drop flashing format the RP2350 bootrom understands: a
sequence of 512-byte blocks, each carrying up to 256 bytes of payload and the
absolute target address.  The RP2350 *data* family id marks blocks that carry
no executable image, so the bootrom writes them without treating the file as
new firmware.  Reference: https://github.com/microsoft/uf2 (spec) and the
RP2350 datasheet, section 5.5 (UF2 targeting).
"""

from __future__ import annotations

import enum
import struct
from dataclasses import dataclass, field

UF2_MAX_PAYLOAD_SIZE: int = 476
UF2_FAMILY_ID_RP2350_DATA: int = 0xE48BFF58


class UF2Flags(enum.IntFlag):
    FAMILY_ID_PRESENT = 0x00002000
    NO_FIRMWARE_FLASH = 0x00001000
    NOT_MAIN_FLASH = 0x00000800
    FILE_CONTAINER = 0x00000400
    MD5_CHECKSUM = 0x00000200
    NO_FAT = 0x00000100
    NOT_EXTRACTING = 0x00000080
    USB_INTERFACE = 0x0000007F


@dataclass(frozen=True)
class UF2Block:
    magic1: int = 0x0A324655
    magic2: int = 0x9E5D5157
    flags: int = UF2Flags.FAMILY_ID_PRESENT
    target_address: int = 0
    payload_size: int = 0
    block_no: int = 0
    block_count: int = 0
    family_id: int = UF2_FAMILY_ID_RP2350_DATA
    data: bytes = field(default_factory=bytes)
    magic3: int = 0x0AB16F30

    def to_bytes(self) -> bytes:
        if len(self.data) > UF2_MAX_PAYLOAD_SIZE:
            raise ValueError(f"data size {len(self.data)} exceeds the UF2 payload limit {UF2_MAX_PAYLOAD_SIZE}")
        padding: int = UF2_MAX_PAYLOAD_SIZE - len(self.data)
        struct_str: str = f"<IIIIIIII{self.payload_size}s{padding}xI"
        return struct.pack(struct_str, *self.__dict__.values())


@dataclass(frozen=True)
class UF2Packer:
    page_size: int = 256
    data: bytes = field(default_factory=bytes)
    start_address: int = 0

    def to_uf2(self) -> bytes:
        """Every block carries a full page: picotool and the bootrom skip a
        short final block (seen: a 128-byte tail never reached flash), so the
        image is padded to the page size with 0xFF, the erased-flash value."""
        block_count: int = (len(self.data) + self.page_size - 1) // self.page_size
        padded: bytes = self.data + bytes([0xFF]) * (block_count * self.page_size - len(self.data))
        out: bytearray = bytearray()
        count: int
        idx: int
        for count, idx in enumerate(range(0, len(padded), self.page_size)):
            block_data: bytes = padded[idx : idx + self.page_size]
            block: UF2Block = UF2Block(
                target_address=self.start_address + idx,
                payload_size=len(block_data),
                block_no=count,
                block_count=block_count,
                data=block_data,
            )
            out.extend(block.to_bytes())
        return bytes(out)
