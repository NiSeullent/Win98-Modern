"""Build a Joliet test CD from an explicit allowlist of Win98 test files.

This is a non-bootable data CD. Installation media and product keys must never
be placed in the input directory. Requires pycdlib 1.20.0.
"""

from __future__ import annotations

import argparse
from io import BytesIO
from pathlib import Path

import pycdlib


ALLOWED = (
    "m98wrap.dll",
    "smoke.exe",
    "IMPROBE.EXE",
    "import_probe.exe",
    "memprobe.exe",
    "memstress.exe",
    "cpuapp.exe",
    "app-profiles.ini",
    "core.ini",
    "KEX452.EXE",
)


def iso_alias(index: int, name: str) -> str:
    stem, dot, suffix = name.upper().rpartition(".")
    if dot and 1 <= len(stem) <= 8 and 1 <= len(suffix) <= 3:
        if all(char.isascii() and (char.isalnum() or char == "_") for char in stem + suffix):
            return f"/{stem}.{suffix};1"
    return f"/FILE{index:04d}.DAT;1"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="staged vm/share directory")
    parser.add_argument("output", type=Path, help="non-bootable test ISO path")
    args = parser.parse_args()

    source = args.source.resolve(strict=True)
    if not source.is_dir():
        parser.error("source must be a directory")
    output = args.output.resolve()
    if source == output.parent or source in output.parents:
        parser.error("output must be outside the source directory")
    files = sorted(
        (entry for entry in source.iterdir() if entry.is_file()),
        key=lambda path: path.name.lower(),
    )
    if not files:
        parser.error("source has no files")
    allowed = {name.lower() for name in ALLOWED}
    unexpected = [entry.name for entry in files if entry.name.lower() not in allowed]
    if unexpected:
        parser.error("unexpected file(s) in source: " + ", ".join(unexpected))
    if "app-profiles.ini" not in {entry.name.lower() for entry in files}:
        parser.error("app-profiles.ini is missing; CPUAPP needs the Joliet name")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".partial")
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, joliet=3, vol_ident="WIN98TEST")
    try:
        for index, entry in enumerate(files, 1):
            iso.add_file(
                str(entry),
                iso_path=iso_alias(index, entry.name),
                joliet_path="/" + entry.name,
            )
        iso.write(str(temporary))
    finally:
        iso.close()
    temporary.replace(output)

    check = pycdlib.PyCdlib()
    check.open(str(output))
    try:
        for entry in files:
            payload = BytesIO()
            check.get_file_from_iso_fp(payload, joliet_path="/" + entry.name)
            if payload.getvalue() != entry.read_bytes():
                raise RuntimeError(f"ISO payload mismatch for {entry.name}")
    finally:
        check.close()
    print(f"Created {output} with {len(files)} Joliet file(s)")
    for entry in files:
        print(entry.name)


if __name__ == "__main__":
    main()
