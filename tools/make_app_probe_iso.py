"""Build a read-only Joliet CD from an extracted portable application.

Use only publisher packages staged in the ignored vm/share directory. The
result is a test fixture, not a redistributable project release.
"""

from __future__ import annotations

import argparse
from io import BytesIO
from pathlib import Path

import pycdlib


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="extracted application directory")
    parser.add_argument("output", type=Path, help="ISO file outside source")
    parser.add_argument("--prefix", default="app", help="short root directory on CD")
    args = parser.parse_args()

    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    if not source.is_dir() or source == output.parent or source in output.parents:
        parser.error("source must be a directory and output must be outside it")
    if not (1 <= len(args.prefix) <= 8 and args.prefix.isascii() and
            args.prefix.isalnum()):
        parser.error("prefix must be 1-8 ASCII letters or digits")

    entries = sorted(source.rglob("*"), key=lambda p: (len(p.relative_to(source).parts), str(p).lower()))
    if not entries or len(entries) > 10000:
        parser.error("source must contain between 1 and 10000 entries")
    if any(path.is_symlink() or not (path.is_dir() or path.is_file()) for path in entries):
        parser.error("source contains an unsupported entry or symbolic link")
    files = [path for path in entries if path.is_file()]
    if not files or sum(path.stat().st_size for path in files) > 2 * 1024**3:
        parser.error("source must contain files totaling at most 2 GiB")
    relative_names = [str(path.relative_to(source)).replace("\\", "/").casefold() for path in entries]
    if len(set(relative_names)) != len(relative_names):
        parser.error("source contains names that collide on Windows")

    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, joliet=3, vol_ident="APPTEST")
    iso.add_directory(iso_path=f"/{args.prefix.upper()}", joliet_path=f"/{args.prefix}")
    aliases: dict[Path, str] = {source: f"/{args.prefix.upper()}"}
    count = 0
    try:
        for entry in entries:
            parent = aliases[entry.parent]
            joliet = "/" + args.prefix + "/" + entry.relative_to(source).as_posix()
            count += 1
            if entry.is_dir():
                alias = f"{parent}/D{count:04d}"
                iso.add_directory(iso_path=alias, joliet_path=joliet)
                aliases[entry] = alias
            else:
                iso.add_file(str(entry), iso_path=f"{parent}/F{count:04d}.DAT;1", joliet_path=joliet)
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_name(output.name + ".partial")
        iso.write(str(temporary))
    finally:
        iso.close()
    temporary.replace(output)

    check = pycdlib.PyCdlib()
    check.open(str(output))
    try:
        for entry in files:
            data = BytesIO()
            joliet = "/" + args.prefix + "/" + entry.relative_to(source).as_posix()
            check.get_file_from_iso_fp(data, joliet_path=joliet)
            if data.getvalue() != entry.read_bytes():
                raise RuntimeError(f"ISO payload mismatch: {joliet}")
    finally:
        check.close()
    print(f"Created {output}: {len(files)} files verified")


if __name__ == "__main__":
    main()
