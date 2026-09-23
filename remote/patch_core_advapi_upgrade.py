"""Move an installed KernelEx ADVAPI32 route to a versioned A/W library.

Operate on a freshly downloaded CORE.INI. Preserve encoding and line endings,
reject unexpected section/route counts, and never rewrite unrelated entries.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}
W_ROUTE = b"ADVAPI32.RegGetValueW="
A_ROUTE = b"ADVAPI32.RegGetValueA="


def patch(data: bytes, old: str = "m98adv", new: str = "m98ad2") -> bytes:
    for value in (old, new):
        if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", value):
            raise ValueError("library must be a lowercase DOS 8.3 stem")
    if old == new:
        raise ValueError("old and new library names must differ")
    old_b, new_b = old.encode("ascii"), new.encode("ascii")
    section = b""
    contents_count = 0
    routed: set[bytes] = set()
    output: list[bytes] = []
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        heading = re.fullmatch(rb"\[([^\]]+)\]", body)
        if heading:
            section = heading.group(1)
        if section == b"DCFG1" and body.startswith(b"contents="):
            tokens = body[len(b"contents="):].split(b",")
            if contents_count or tokens.count(old_b) != 1 or new_b in tokens:
                raise ValueError("unexpected DCFG1 contents")
            tokens[tokens.index(old_b)] = new_b
            output.append(b"contents=" + b",".join(tokens) + ending)
            contents_count += 1
            continue
        if section in SECTIONS:
            if body.startswith(A_ROUTE):
                raise ValueError("ANSI route already exists")
            if body.startswith(W_ROUTE):
                if section in routed or body != W_ROUTE + old_b + b".0" or not ending:
                    raise ValueError("unexpected Unicode route")
                output.append(A_ROUTE + new_b + b".0" + ending)
                output.append(W_ROUTE + new_b + b".0" + ending)
                routed.add(section)
                continue
        output.append(line)
    if contents_count != 1 or routed != SECTIONS:
        raise ValueError("missing contents or route sections")
    return b"".join(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--old", default="m98adv")
    parser.add_argument("--new", default="m98ad2")
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
        "route_sections": sorted(x.decode() for x in SECTIONS),
    }, indent=2))


if __name__ == "__main__":
    main()
