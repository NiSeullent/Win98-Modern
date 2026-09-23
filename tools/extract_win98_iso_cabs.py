"""Copy the 76 pinned Win98 CABs from the supplied ISO into ignored media.

No Microsoft CAB is committed. Existing matching files are left untouched.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path

import pycdlib


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    expected_iso = manifest["iso_sha256"]
    digest = hashlib.sha256(args.iso.read_bytes()).hexdigest()
    if digest != expected_iso:
        raise ValueError(f"ISO SHA256 differs: {digest}")
    cabs: dict[str, str] = manifest["cab_file_sha256"]
    if len(cabs) != manifest["cab_count"]:
        raise ValueError("manifest CAB count is inconsistent")
    if not args.verify_only:
        args.out_dir.mkdir(parents=True, exist_ok=True)
    iso = pycdlib.PyCdlib()
    iso.open(str(args.iso))
    try:
        for name, expected in sorted(cabs.items()):
            data = io.BytesIO()
            iso.get_file_from_iso_fp(data, iso_path=f"/WIN98/{name}")
            actual = hashlib.sha256(data.getvalue()).hexdigest()
            if actual != expected:
                raise ValueError(f"ISO CAB SHA256 differs: {name}")
            target = args.out_dir / name
            if target.exists():
                current = hashlib.sha256(target.read_bytes()).hexdigest()
                if current != expected:
                    raise ValueError(f"existing CAB SHA256 differs: {target}")
            elif args.verify_only:
                raise ValueError(f"missing local CAB: {target}")
            else:
                target.write_bytes(data.getvalue())
    finally:
        iso.close()
    print(f"Verified {len(cabs)} CABs against {args.iso} and {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
