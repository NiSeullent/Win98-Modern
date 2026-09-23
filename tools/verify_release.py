"""Verify a release ZIP against both its external and internal SHA-256 manifests."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import zipfile


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def verify(path: Path) -> None:
    archive_hash = digest(path.read_bytes())
    checksum_path = path.with_name(path.name + ".sha256")
    expected_line = f"{archive_hash}  {path.name}"
    if checksum_path.read_text(encoding="utf-8").strip() != expected_line:
        raise ValueError("external SHA-256 mismatch")

    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or "SHA256SUMS.txt" not in names:
            raise ValueError("missing manifest or duplicate ZIP member")
        for name in names:
            member = PurePosixPath(name)
            if (member.is_absolute() or ".." in member.parts or
                    "\\" in name or name.endswith("/")):
                raise ValueError(f"unsafe ZIP member: {name}")
        manifest = archive.read("SHA256SUMS.txt").decode("utf-8")
        listed: dict[str, str] = {}
        for line in manifest.splitlines():
            hash_value, separator, name = line.partition("  ")
            if (separator != "  " or len(hash_value) != 64 or
                    any(c not in "0123456789abcdef" for c in hash_value) or
                    not name or name in listed):
                raise ValueError("invalid internal manifest")
            listed[name] = hash_value
        if set(listed) != set(names) - {"SHA256SUMS.txt"}:
            raise ValueError("internal manifest member list mismatch")
        for name, expected_hash in listed.items():
            if digest(archive.read(name)) != expected_hash:
                raise ValueError(f"internal SHA-256 mismatch: {name}")
    print(f"PASS: {len(listed)} files, external and internal SHA-256: {archive_hash}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zip", type=Path)
    args = parser.parse_args()
    verify(args.zip)


if __name__ == "__main__":
    main()
