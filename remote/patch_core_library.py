"""Replace one loaded KernelEx API library in installed CORE.INI, byte preserving."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


def patch(data: bytes, old_library: str, new_library: str) -> bytes:
    for library in (old_library, new_library):
        if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
            raise ValueError("library must be a lowercase DOS 8.3 stem")
    if old_library == new_library:
        raise ValueError("old and new library must differ")
    section = b""
    replacements = 0
    output: list[bytes] = []
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        match = re.fullmatch(rb"\[([^\]]+)\]", body)
        if match:
            section = match.group(1)
        elif section == b"DCFG1" and body.startswith(b"contents="):
            libraries = body[len(b"contents="):].split(b",")
            old = old_library.encode("ascii")
            new = new_library.encode("ascii")
            if replacements or libraries.count(old) != 1 or new in libraries:
                raise ValueError("unexpected or duplicate DCFG1 contents")
            libraries[libraries.index(old)] = new
            line = b"contents=" + b",".join(libraries) + ending
            replacements += 1
        output.append(line)
    if replacements != 1:
        raise ValueError("installed CORE.INI lacks a unique DCFG1 contents line")
    return b"".join(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--old-library", required=True)
    parser.add_argument("--library", required=True)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    if source == output or output.exists():
        parser.error("output must be a new path distinct from source")
    original = source.read_bytes()
    updated = patch(original, args.old_library, args.library)
    with output.open("xb") as stream:
        stream.write(updated)
    print(json.dumps({
        "source_sha256": hashlib.sha256(original).hexdigest(),
        "output_sha256": hashlib.sha256(updated).hexdigest(),
        "old_library": args.old_library,
        "library": args.library,
    }, indent=2))


if __name__ == "__main__":
    main()
