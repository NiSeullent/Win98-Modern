"""Pin the five work-object APIs to the tested KernelEx provider.

The installed KernelEx can expose an earlier CreateThreadpoolWork entry that
returns NULL. Explicit per-profile routes avoid provider ordering ambiguity.
Keep all unrelated CORE.INI bytes, including its original line endings.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}
NAMES = (
    b"CloseThreadpoolWork",
    b"CreateThreadpoolWork",
    b"FreeLibraryWhenCallbackReturns",
    b"SubmitThreadpoolWork",
    b"WaitForThreadpoolWorkCallbacks",
)
ANCHOR = b"KERNEL32.LCMapStringEx="


def patch(data: bytes, library: str) -> bytes:
    if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
        raise ValueError("library must be a lowercase DOS 8.3 stem")
    lib = library.encode("ascii")
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
            libraries = body[len(b"contents="):].split(b",")
            if contents_count or libraries.count(lib) != 1:
                raise ValueError("unexpected provider contents")
            contents_count += 1
        if section in SECTIONS:
            if any(body.startswith(b"KERNEL32." + name + b"=") for name in NAMES):
                raise ValueError("threadpool route already exists")
            if body.startswith(ANCHOR):
                if section in routed or body != ANCHOR + lib + b".0" or not ending:
                    raise ValueError("unexpected NLS anchor")
                output.append(line)
                for name in NAMES:
                    output.append(b"KERNEL32." + name + b"=" + lib + b".0" + ending)
                routed.add(section)
                continue
        output.append(line)
    if contents_count != 1 or routed != SECTIONS:
        raise ValueError("missing provider or required profile section")
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
        "route_count": len(SECTIONS) * len(NAMES),
    }, indent=2))


if __name__ == "__main__":
    main()
