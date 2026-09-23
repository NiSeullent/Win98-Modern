"""Verify the wrapper and static GetProductInfo probe remain Win98 loadable.

Static inspection cannot prove KernelEx runtime routing or SKU semantics.
"""

from __future__ import annotations

import json
from pathlib import Path

import pefile

from check_pe98 import check as check_wrapper


def imported(pe: pefile.PE) -> dict[str, set[str]]:
    return {
        descriptor.dll.decode("ascii").upper(): {
            entry.name.decode("ascii") for entry in descriptor.imports
            if entry.name is not None
        }
        for descriptor in pe.DIRECTORY_ENTRY_IMPORT
    }


def check(root: Path) -> list[str]:
    dll_path = root / "build/productinfo/m98wrap.dll"
    probe_path = root / "build/productinfo/productinfo_import_probe.exe"
    manifest = json.loads((root / "benchmarks/win98se-ko-oem-native-exports-v1.json").read_text(encoding="utf-8"))
    native = set(manifest["dlls"]["KERNEL32.DLL"])
    issues = check_wrapper(dll_path)
    with pefile.PE(str(dll_path)) as dll:
        imports = imported(dll)
        if set(imports) != {"KERNEL32.DLL"}:
            issues.append(f"wrapper import DLL set: {set(imports)}")
        for name in sorted(imports.get("KERNEL32.DLL", set()) - native):
            issues.append(f"wrapper uses non-OEM import: {name}")
    with pefile.PE(str(probe_path)) as probe:
        oh = probe.OPTIONAL_HEADER
        if probe.FILE_HEADER.Machine != 0x14C or oh.Magic != 0x10B:
            issues.append("probe is not i386 PE32")
        if oh.Subsystem != 3 or (oh.MajorSubsystemVersion, oh.MinorSubsystemVersion) != (4, 10):
            issues.append("probe subsystem is not console 4.10")
        if oh.DllCharacteristics & (0x0040 | 0x0100):
            issues.append("probe requires ASLR or NX")
        for index in (9, 13, 14):
            if oh.DATA_DIRECTORY[index].VirtualAddress:
                issues.append(f"probe unsupported data directory {index}")
        imports = imported(probe)
        if set(imports) != {"KERNEL32.DLL"}:
            issues.append(f"probe import DLL set: {set(imports)}")
        elif "GetProductInfo" not in imports["KERNEL32.DLL"]:
            issues.append("GetProductInfo is not statically imported")
        for name in sorted(imports.get("KERNEL32.DLL", set()) - native - {"GetProductInfo"}):
            issues.append(f"probe uses non-OEM import: {name}")
    return issues


if __name__ == "__main__":
    errors = check(Path(__file__).resolve().parents[1])
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        raise SystemExit(1)
    print("PASS: GetProductInfo Win98 PE/import static gate")
