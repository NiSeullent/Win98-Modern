"""Version an installed KernelEx Shell library while preserving CORE.INI bytes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}
CONTENTS_PREFIX = b"contents=std,kexbases,kexbasen,m98wrap,"
PARSE = b"SHELL32.SHParseDisplayName="
SELECT = b"SHELL32.SHOpenFolderAndSelectItems="


def valid_stem(stem: str) -> bytes:
    if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", stem):
        raise ValueError("library must be a lowercase DOS 8.3 stem")
    return stem.encode("ascii")


def patch(data: bytes, old_library: str, new_library: str) -> bytes:
    old, new = valid_stem(old_library), valid_stem(new_library)
    if old == new:
        raise ValueError("old and new library must differ")
    lines = data.splitlines(keepends=True)
    output: list[bytes] = []
    section = b""
    seen_contents = False
    seen_parse: set[bytes] = set()
    seen_select: set[bytes] = set()
    for line in lines:
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        match = re.fullmatch(rb"\[([^\]]+)\]", body)
        if match:
            section = match.group(1)
            output.append(line)
            continue
        if section == b"DCFG1" and body.startswith(CONTENTS_PREFIX):
            if seen_contents or body != CONTENTS_PREFIX + old:
                raise ValueError("unexpected or duplicate DCFG1 contents")
            output.append(CONTENTS_PREFIX + new + ending)
            seen_contents = True
            continue
        if section in SECTIONS and body.startswith(SELECT):
            if section in seen_select or body != SELECT + old + b".0":
                raise ValueError("unexpected or duplicate select route in " + section.decode())
            output.append(SELECT + new + b".0" + ending)
            seen_select.add(section)
            continue
        if section in SECTIONS and body.startswith(PARSE):
            if section in seen_parse or body != PARSE + old + b".0":
                raise ValueError("unexpected or duplicate parse route in " + section.decode())
            if section not in seen_select:
                if not ending:
                    raise ValueError("cannot insert selection route without a line ending")
                output.append(SELECT + new + b".0" + ending)
                seen_select.add(section)
            output.append(PARSE + new + b".0" + ending)
            seen_parse.add(section)
            continue
        output.append(line)
    if not seen_contents or seen_parse != SECTIONS or seen_select != SECTIONS:
        raise ValueError("installed CORE.INI lacks a required Shell route or contents")
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
        "changed_sections": sorted(x.decode() for x in SECTIONS),
        "old_library": args.old_library,
        "library": args.library,
    }, indent=2))


if __name__ == "__main__":
    main()
