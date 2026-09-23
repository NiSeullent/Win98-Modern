"""Rebuild a names-only Windows 98 SE ISO PE export inventory.

Input CABs and extracted files must come from the supplied ISO. The script
verifies all 76 CAB bytes against the ISO; it does not package Microsoft code.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import struct
from collections import defaultdict
from pathlib import Path

import pycdlib

from measure_pe_coverage import CoverageError, SCHEMA, pe_exports, sha256_file
from scan_imports import PEError


def cab_members(path: Path) -> list[str]:
    data = path.read_bytes()
    if len(data) < 36 or data[:4] != b"MSCF":
        raise ValueError(f"not a CAB: {path}")
    count = struct.unpack_from("<H", data, 28)[0]
    cursor = struct.unpack_from("<I", data, 16)[0]
    names: list[str] = []
    for _ in range(count):
        if cursor + 16 > len(data):
            raise ValueError(f"truncated CFFILE in {path}")
        end = data.find(b"\0", cursor + 16)
        if end < 0:
            raise ValueError(f"unterminated CFFILE name in {path}")
        name = data[cursor + 16:end].decode("cp437")
        names.append(name.replace("\\", "/").rsplit("/", 1)[-1].upper())
        cursor = end + 1
    return names


def build(iso_path: Path, cab_dir: Path, extracted_dir: Path) -> dict:
    cabs = sorted(cab_dir.glob("*.CAB"))
    if not cabs:
        raise ValueError("no CAB files supplied")
    iso = pycdlib.PyCdlib()
    iso.open(str(iso_path))
    cab_hashes: dict[str, str] = {}
    member_cabs: dict[str, set[str]] = defaultdict(set)
    record_count = 0
    try:
        for cab in cabs:
            from_iso = io.BytesIO()
            iso.get_file_from_iso_fp(from_iso, iso_path=f"/WIN98/{cab.name.upper()}")
            iso_digest = hashlib.sha256(from_iso.getvalue()).hexdigest()
            local_digest = sha256_file(cab)
            if iso_digest != local_digest:
                raise ValueError(f"CAB differs from ISO: {cab.name}")
            cab_hashes[cab.name.upper()] = local_digest
            for member in cab_members(cab):
                record_count += 1
                member_cabs[member].add(cab.name.upper())
    finally:
        iso.close()

    extracted = {path.name.upper(): path for path in extracted_dir.rglob("*")
                 if path.is_file()}
    missing = sorted(set(member_cabs) - set(extracted))
    unexpected = sorted(set(extracted) - set(member_cabs))
    if missing != ["GM16.DLS"] or unexpected:
        raise ValueError(f"incomplete/unexpected extraction: missing={missing}, "
                         f"unexpected={unexpected[:10]}")
    dlls: dict[str, list[str]] = {}
    modules: dict[str, dict] = {}
    skipped_mz = 0
    for name, path in sorted(extracted.items()):
        with path.open("rb") as stream:
            if stream.read(2) != b"MZ":
                continue
        try:
            exports = pe_exports(path, name)
        except (CoverageError, PEError, OSError):
            skipped_mz += 1  # NE, LE, non-x86 PE, or no export directory
            continue
        identities = sorted(exports[name])
        dlls[name] = identities
        modules[name] = {
            "sha256": sha256_file(path),
            "size": path.stat().st_size,
            "cab_files": sorted(member_cabs[name]),
            "export_identity_count": len(identities),
        }
    if not dlls:
        raise ValueError("no PE32 i386 exports found")
    return {
        "schema": SCHEMA,
        "baseline_version": 1,
        "evidence_kind": "ISO CAB PE export names; optional modules may be absent from installed guest",
        "provenance": ("Windows 98 SE Korean OEM ISO native PE32 i386 exports; "
                       f"ISO SHA256 {sha256_file(iso_path)}; CAB bytes verified against ISO"),
        "iso_sha256": sha256_file(iso_path),
        "cab_count": len(cabs),
        "cab_file_sha256": cab_hashes,
        "cab_file_records": record_count,
        "unique_cab_members": len(member_cabs),
        "extracted_members": len(extracted),
        "missing_non_pe_members": missing,
        "skipped_mz_without_pe32_exports": skipped_mz,
        "modules": modules,
        "dlls": dlls,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--cab-dir", required=True, type=Path)
    parser.add_argument("--extracted-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    manifest = build(args.iso, args.cab_dir, args.extracted_dir)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print(f"{args.out}: {len(manifest['dlls'])} export modules, "
          f"{sum(map(len, manifest['dlls'].values()))} identities; "
          f"{manifest['cab_file_records']} CAB records, "
          f"{manifest['extracted_members']} extracted members")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
