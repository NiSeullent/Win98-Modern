"""Two same-size PNG icons with 16/32bpp directory metadata for guest probe."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from make_comctl_png_fixture import png


def ico_bytes() -> bytes:
    images = (png(32, (255, 0, 0)), png(32, (0, 0, 255)))
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = bytearray()
    for bits, payload in zip((16, 32), images):
        entries.extend(struct.pack("<BBBBHHII", 32, 32, 0, 0, 1, bits,
                                   len(payload), offset))
        offset += len(payload)
    return header + entries + b"".join(images)


if __name__ == "__main__":
    target = Path(sys.argv[1])
    target.write_bytes(ico_bytes())
    red = png(32, (255, 0, 0))
    blue = png(32, (0, 0, 255))
    group = (struct.pack("<HHH", 0, 1, 2)
             + struct.pack("<BBBBHHIH", 32, 32, 0, 0, 1, 16, len(red), 1)
             + struct.pack("<BBBBHHIH", 32, 32, 0, 0, 1, 32, len(blue), 2))
    (target.parent / "comctl_png_depth_red.png").write_bytes(red)
    (target.parent / "comctl_png_depth_blue.png").write_bytes(blue)
    (target.parent / "comctl_png_depth_group.bin").write_bytes(group)
