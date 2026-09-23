"""Atomically prepare a versioned KernelEx wrapper upgrade in CORE.INI.

Discover all routes owned by the old provider and require the same route map
in all three active profiles. Preserve library table indices and unrelated
bytes. A fixed list silently stranded newly added families on the old DLL.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}


def patch(data: bytes, old: str, new: str) -> bytes:
    for library in (old, new):
        if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
            raise ValueError("library must be a lowercase DOS 8.3 stem")
    if old == new:
        raise ValueError("old and new library names must differ")
    old_bytes, new_bytes = old.encode("ascii"), new.encode("ascii")
    section = b""
    contents_count = 0
    seen_sections: set[bytes] = set()
    routes: dict[bytes, dict[bytes, bytes]] = {key: {} for key in SECTIONS}
    keys: dict[bytes, set[bytes]] = {key: set() for key in SECTIONS}
    provider_reference = re.compile(re.escape(old_bytes) + rb"\.(\d+)")
    output: list[bytes] = []
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        heading = re.fullmatch(rb"\[([^\]]+)\]", body)
        if heading:
            section = heading.group(1)
            if section in SECTIONS or section == b"DCFG1":
                if section in seen_sections:
                    raise ValueError("duplicate provider/profile section")
                seen_sections.add(section)
        if section == b"DCFG1" and body.startswith(b"contents="):
            libraries = body[len(b"contents="):].split(b",")
            if contents_count or libraries.count(old_bytes) != 1 or new_bytes in libraries:
                raise ValueError("unexpected provider contents")
            libraries[libraries.index(old_bytes)] = new_bytes
            output.append(b"contents=" + b",".join(libraries) + ending)
            contents_count += 1
            continue
        if body.startswith((b";", b"#")):
            output.append(line)
            continue
        match = provider_reference.fullmatch(body.partition(b"=")[2])
        if section in SECTIONS and b"=" in body and not body.startswith((b";", b"#")):
            key = body.split(b"=", 1)[0]
            if key in keys[section]:
                raise ValueError("duplicate API route")
            keys[section].add(key)
            if match:
                if not re.fullmatch(rb"[A-Za-z0-9_]+\.[A-Za-z0-9_@$?]+", key):
                    raise ValueError("unrecognized provider route")
                routes[section][key] = match.group(1)
                output.append(key + b"=" + new_bytes + b"." + match.group(1) + ending)
                continue
        if match:
            raise ValueError("provider reference outside supported profiles")
        # Reject malformed/whitespace-suffixed ownership instead of leaving a
        # dangling reference when removing the old provider from contents.
        if not body.startswith((b";", b"#")) and b"=" + old_bytes + b"." in body:
            raise ValueError("malformed provider reference")
        output.append(line)
    first = routes[b"DCFG1.names.98"]
    if (contents_count != 1 or seen_sections != SECTIONS | {b"DCFG1"} or
            not first or any(value != first for value in routes.values())):
        raise ValueError("missing provider/profile or inconsistent family routes")
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
        "route_count": sum(before != after and not before.startswith(b"contents=")
                           for before, after in zip(original.splitlines(), updated.splitlines())),
    }, indent=2))


if __name__ == "__main__":
    main()
