"""Generate two-color ICO fixture for ordinal 381 group-selection tests."""
from __future__ import annotations

import struct
from pathlib import Path
import sys


def image(edge: int, red: int, green: int, blue: int) -> bytes:
    header = struct.pack("<IiiHHIIiiII", 40, edge, edge * 2, 1, 32, 0,
                         edge * edge * 4, 0, 0, 0, 0)
    pixel = bytes((blue, green, red, 255))
    xor = pixel * (edge * edge)
    mask_stride = ((edge + 31) // 32) * 4
    return header + xor + bytes(mask_stride * edge)


def ico_bytes() -> bytes:
    images = (image(16, 255, 0, 0), image(48, 0, 0, 255))
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    directory = bytearray()
    for edge, bits in zip((16, 48), images):
        directory.extend(struct.pack("<BBBBHHII", edge, edge, 0, 0, 1, 32,
                                     len(bits), offset))
        offset += len(bits)
    return header + directory + b"".join(images)


if __name__ == "__main__":
    Path(sys.argv[1]).write_bytes(ico_bytes())
