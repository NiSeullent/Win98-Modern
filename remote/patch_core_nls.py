"""Route locale-name Ex APIs to one verified KernelEx provider, preserving bytes.

The original KernelEx installation can supply a callable but unimplemented
CompareStringEx entry. An extra provider in contents= alone does not guarantee
that its static import wins. Explicit per-profile routes select the tested
wrapper while retaining every unrelated installed CORE.INI setting.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}
ROUTES = (b"KERNEL32.CompareStringEx", b"KERNEL32.LCMapStringEx")
ANCHOR = b"ADVAPI32.RegGetValueW="


def patch(data: bytes, library: str) -> bytes:
    if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
        raise ValueError("library must be a lowercase DOS 8.3 stem")
    lib = library.encode("ascii")
    section = b""
    seen_contents = 0
    patched: set[bytes] = set()
    output: list[bytes] = []
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        heading = re.fullmatch(rb"\[([^\]]+)\]", body)
        if heading:
            section = heading.group(1)
        if section == b"DCFG1" and body.startswith(b"contents="):
            if seen_contents or body[len(b"contents="):].split(b",").count(lib) != 1:
                raise ValueError("unexpected DCFG1 provider contents")
            seen_contents += 1
        if section in SECTIONS:
            if any(body.startswith(route + b"=") for route in ROUTES):
                raise ValueError("locale-name route already exists")
            if body.startswith(ANCHOR):
                if section in patched or not ending:
                    raise ValueError("duplicate anchor or missing line ending")
                output.append(line)
                for route in ROUTES:
                    output.append(route + b"=" + lib + b".0" + ending)
                patched.add(section)
                continue
        output.append(line)
    if seen_contents != 1 or patched != SECTIONS:
        raise ValueError("missing provider or required route section")
    return b"".join(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--library", required=True)
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
        "route_sections": sorted(x.decode("ascii") for x in SECTIONS),
    }, indent=2))


if __name__ == "__main__":
    main()
