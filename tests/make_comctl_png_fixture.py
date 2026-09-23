"""Create deterministic PNG-compressed ICO groups for Win98 ordinal 381 tests."""
from __future__ import annotations

import struct
import sys
from pathlib import Path
import zlib


def chunk(kind: bytes, payload: bytes) -> bytes:
    data = kind + payload
    return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data))


def png(edge: int, color: tuple[int, int, int]) -> bytes:
    rows = bytearray()
    for y in range(edge):
        rows.append(0)  # PNG filter None.
        for x in range(edge):
            # Transparent corner makes the resulting AND mask observable.
            rows.extend((*color, 0 if x == 0 and y == 0 else 255))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", edge, edge, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
            + chunk(b"IEND", b""))


def ico_bytes() -> bytes:
    images = (png(16, (255, 0, 0)), png(48, (0, 0, 255)))
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = bytearray()
    for edge, payload in zip((16, 48), images):
        entries.extend(struct.pack("<BBBBHHII", edge, edge, 0, 0, 1, 32,
                                   len(payload), offset))
        offset += len(payload)
    return header + entries + b"".join(images)


if __name__ == "__main__":
    Path(sys.argv[1]).write_bytes(ico_bytes())
