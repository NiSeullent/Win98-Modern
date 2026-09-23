"""Rebuild a names-only KernelEx API table declaration inventory.

This is source evidence only. CORE.INI variants and host OS overrides can
disable or replace entries, and source declarations do not prove installation.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from measure_pe_coverage import SCHEMA, sha256_file, source_exports


def build(root: Path) -> dict:
    apilibs = root / "apilibs"
    core_ini = apilibs / "CORE.INI"
    files = sorted((apilibs / "kexbases").rglob("*_apilist.c"))
    files += sorted((apilibs / "kexbasen").rglob("*_apilist.c"))
    if not files or not core_ini.is_file():
        raise ValueError("KernelEx source checkout lacks API tables or CORE.INI")
    source = source_exports(str(file) for file in files)
    commit = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    return {
        "schema": SCHEMA,
        "baseline_version": 1,
        "evidence_kind": "KernelEx source API-table declarations, not installed exports",
        "provenance": ("KernelEx source DECL_API/DECL_TAB declarations from kexbases and "
                       f"kexbasen at git commit {commit}; candidate names only"),
        "git_commit": commit,
        "configuration_file": "apilibs/CORE.INI",
        "configuration_file_sha256": sha256_file(core_ini),
        "configuration_caveat": (
            "CORE.INI DCFG1 lists std,kexbases,kexbasen but mode and Windows 98 "
            "overrides may disable or replace names; this inventory is an upper "
            "bound of source declarations, not the enabled API set."),
        "source_files": {
            file.relative_to(root).as_posix(): sha256_file(file)
            for file in files
        },
        "dlls": {dll: sorted(names) for dll, names in sorted(source.items())},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    manifest = build(args.source_root)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print(f"{args.out}: {len(manifest['source_files'])} source files, "
          f"{len(manifest['dlls'])} DLL names, "
          f"{sum(map(len, manifest['dlls'].values()))} declarations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
