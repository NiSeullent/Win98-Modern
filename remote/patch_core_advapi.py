"""Add one versioned Win98 ADVAPI32 API library to installed KernelEx CORE.INI.

Preserves all existing bytes and only inserts the validated library name and
three RegGetValueW routes. Work on a freshly downloaded, hashed guest file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


ROUTE_SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}
ANCHOR = b"SHELL32.SHParseDisplayName="
ROUTE = b"ADVAPI32.RegGetValueW="


def patch(data: bytes, library: str = "m98adv") -> bytes:
    if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
        raise ValueError("library must be a lowercase DOS 8.3 stem")
    name = library.encode("ascii")
    section = b""
    seen_contents = False
    seen_routes: set[bytes] = set()
    result: list[bytes] = []
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        match = re.fullmatch(rb"\[([^\]]+)\]", body)
        if match:
            section = match.group(1)
        if section == b"DCFG1" and body.startswith(b"contents="):
            if seen_contents or name in body[len(b"contents="):].split(b","):
                raise ValueError("duplicate or unexpected DCFG1 contents")
            if not ending:
                raise ValueError("DCFG1 contents lacks line ending")
            result.append(body + b"," + name + ending)
            seen_contents = True
            continue
        if section in ROUTE_SECTIONS and body.startswith(ROUTE):
            raise ValueError("ADVAPI32 route already present")
        result.append(line)
        if section in ROUTE_SECTIONS and body.startswith(ANCHOR):
            if section in seen_routes or not ending:
                raise ValueError("duplicate Shell anchor or missing line ending")
            result.append(ROUTE + name + b".0" + ending)
            seen_routes.add(section)
    if not seen_contents or seen_routes != ROUTE_SECTIONS:
        raise ValueError("installed CORE.INI lacks expected sections/anchors")
    return b"".join(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--library", default="m98adv")
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    if source == output or output.exists():
        parser.error("output must be a new path distinct from source")
    original = source.read_bytes()
    updated = patch(original, args.library)
    with output.open("xb") as stream:
        stream.write(updated)
    print(json.dumps({
        "source_sha256": hashlib.sha256(original).hexdigest(),
        "output_sha256": hashlib.sha256(updated).hexdigest(),
        "library": args.library,
        "route_sections": sorted(section.decode() for section in ROUTE_SECTIONS),
    }, indent=2))


if __name__ == "__main__":
    main()
