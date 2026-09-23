"""Add COMCTL32 345/381 KernelEx routes to a new byte-preserving CORE.INI copy.

The caller must install the provider as a versioned DOS 8.3 basename and
cold-boot the guest. Existing sections, comments and unrelated routes remain
byte-for-byte unchanged. No guest files are edited by this tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

PROFILE_ANCHORS = {
    b"DCFG1.names.98": b"DCFG1.ordinals.98",
    b"DCFG1.names.Me": b"DCFG1.ordinals.Me",
    b"WINXP.names": b"WINXP.ordinals",
}
ORDINALS = (345, 381)


def _sections(data: bytes) -> list[tuple[bytes, list[bytes]]]:
    sections: list[tuple[bytes, list[bytes]]] = []
    current = b""
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        heading = re.fullmatch(rb"\[([^\]]+)\]", body)
        if heading:
            current = heading.group(1)
            if any(name.lower() == current.lower() for name, _ in sections):
                raise ValueError(f"duplicate section {current!r}")
            sections.append((current, [line]))
        elif sections:
            sections[-1][1].append(line)
        else:
            if body.strip():
                raise ValueError("nonblank text before first section")
            sections.append((b"", [line]))
    return sections


def patch(data: bytes, provider: str) -> bytes:
    if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", provider):
        raise ValueError("provider must be a lowercase DOS 8.3 stem")
    if not data or not data.endswith((b"\n", b"\r")):
        raise ValueError("CORE.INI must be nonempty and newline terminated")
    sections = _sections(data)
    lookup = {name.lower(): i for i, (name, _) in enumerate(sections)}
    if b"dcfg1" not in lookup:
        raise ValueError("missing DCFG1 section")
    contents_lines = sections[lookup[b"dcfg1"]][1]
    match_indices = [i for i, line in enumerate(contents_lines)
                     if line.rstrip(b"\r\n").startswith(b"contents=")]
    if len(match_indices) != 1:
        raise ValueError("DCFG1 must have one contents entry")
    index = match_indices[0]
    original = contents_lines[index]
    body = original.rstrip(b"\r\n")
    ending = original[len(body):]
    libraries = body[len(b"contents="):].split(b",")
    word = provider.encode("ascii")
    if word in libraries or not all(libraries):
        raise ValueError("provider already registered or malformed contents")
    contents_lines[index] = body + b"," + word + ending
    route_lines = [b"COMCTL32." + str(number).encode("ascii") + b"=" +
                   word + b".0\r\n" for number in ORDINALS]
    new_sections: list[tuple[bytes, list[bytes]]] = []
    for name, lines in sections:
        new_sections.append((name, lines))
        if name not in PROFILE_ANCHORS:
            continue
        target = PROFILE_ANCHORS[name]
        if target.lower() in lookup:
            raise ValueError(f"existing ordinal section requires manual review: {target!r}")
        new_sections.append((target, [b"[" + target + b"]\r\n", *route_lines,
                                      b"\r\n"]))
    missing = set(PROFILE_ANCHORS) - {name for name, _ in sections}
    if missing:
        raise ValueError(f"missing profile anchors: {sorted(missing)!r}")
    return b"".join(line for _, lines in new_sections for line in lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--provider", required=True)
    args = parser.parse_args()
    if args.output.exists() or args.source.resolve() == args.output.resolve():
        parser.error("output must be a new path")
    source = args.source.read_bytes()
    try:
        updated = patch(source, args.provider)
    except ValueError as error:
        parser.error(str(error))
    with args.output.open("xb") as stream:
        stream.write(updated)
    print(json.dumps({"source_sha256": hashlib.sha256(source).hexdigest(),
                      "output_sha256": hashlib.sha256(updated).hexdigest(),
                      "provider": args.provider, "routes": 6}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
