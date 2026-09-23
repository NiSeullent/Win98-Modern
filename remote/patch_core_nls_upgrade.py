"""Upgrade a versioned KernelEx provider and its two explicit NLS routes.

Operate on a freshly downloaded CORE.INI, preserve all unrelated bytes, and
reject unexpected contents or route counts before creating the output file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}
ROUTES = {b"KERNEL32.CompareStringEx", b"KERNEL32.LCMapStringEx"}


def patch(data: bytes, old: str, new: str) -> bytes:
    for library in (old, new):
        if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
            raise ValueError("library must be a lowercase DOS 8.3 stem")
    if old == new:
        raise ValueError("old and new library names must differ")
    old_bytes, new_bytes = old.encode("ascii"), new.encode("ascii")
    section = b""
    contents_count = 0
    seen_routes: set[tuple[bytes, bytes]] = set()
    output: list[bytes] = []
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        heading = re.fullmatch(rb"\[([^\]]+)\]", body)
        if heading:
            section = heading.group(1)
        if section == b"DCFG1" and body.startswith(b"contents="):
            libraries = body[len(b"contents="):].split(b",")
            if contents_count or libraries.count(old_bytes) != 1 or new_bytes in libraries:
                raise ValueError("unexpected DCFG1 contents")
            libraries[libraries.index(old_bytes)] = new_bytes
            output.append(b"contents=" + b",".join(libraries) + ending)
            contents_count += 1
            continue
        if section in SECTIONS:
            for route in ROUTES:
                if body.startswith(route + b"="):
                    marker = (section, route)
                    if marker in seen_routes or body != route + b"=" + old_bytes + b".0":
                        raise ValueError("unexpected or duplicate NLS route")
                    output.append(route + b"=" + new_bytes + b".0" + ending)
                    seen_routes.add(marker)
                    break
            else:
                output.append(line)
            continue
        output.append(line)
    expected = {(section_name, route) for section_name in SECTIONS for route in ROUTES}
    if contents_count != 1 or seen_routes != expected:
        raise ValueError("missing provider or NLS route")
    return b"".join(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--old", required=True)
    parser.add_argument("--new", required=True)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    if source == output or output.exists():
        parser.error("output must be a new path distinct from source")
    original = source.read_bytes()
    updated = patch(original, args.old, args.new)
    with output.open("xb") as stream:
        stream.write(updated)
    print(json.dumps({
        "source_sha256": hashlib.sha256(original).hexdigest(),
        "output_sha256": hashlib.sha256(updated).hexdigest(),
        "old_library": args.old,
        "new_library": args.new,
        "route_count": len(SECTIONS) * len(ROUTES),
    }, indent=2))


if __name__ == "__main__":
    main()
