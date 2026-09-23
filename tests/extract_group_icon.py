"""Extract one PE RT_GROUP_ICON and its RT_ICON payloads into a test ICO.

The output is a local build fixture from a caller-provided executable. Do not
commit target-application resources or distribute the fixture.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct

import pefile


def resource(pe: pefile.PE, type_id: int, item_id: int) -> bytes:
    for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if kind.id != type_id:
            continue
        for entry in kind.directory.entries:
            if entry.id != item_id:
                continue
            language = entry.directory.entries[0]
            data = language.data.struct
            return pe.get_data(data.OffsetToData, data.Size)
    raise ValueError(f"resource type {type_id}, id {item_id} is absent")


def extract(executable: Path, group_id: int) -> bytes:
    pe = pefile.PE(str(executable), fast_load=False)
    try:
        group = resource(pe, 14, group_id)
        if len(group) < 6:
            raise ValueError("truncated icon group header")
        reserved, kind, count = struct.unpack_from("<HHH", group)
        if reserved or kind != 1 or not count or len(group) != 6 + 14 * count:
            raise ValueError("invalid icon group directory")
        entries = []
        images = []
        offset = 6 + 16 * count
        for index in range(count):
            pos = 6 + index * 14
            size, image_id = struct.unpack_from("<IH", group, pos + 8)
            payload = resource(pe, 3, image_id)
            if size != len(payload):
                raise ValueError(f"RT_ICON {image_id} size differs from group")
            entries.append(group[pos:pos + 12] + struct.pack("<I", offset))
            images.append(payload)
            offset += len(payload)
        return struct.pack("<HHH", 0, 1, count) + b"".join(entries + images)
    finally:
        pe.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("group_id", type=int)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(extract(args.executable, args.group_id))


if __name__ == "__main__":
    main()
