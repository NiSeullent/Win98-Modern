"""Static PE gate for the bounded GetFinalPathNameByHandleW wrapper.

The wrapper is linked only against original Win98 OEM KERNEL32 exports. The
import probe deliberately requests the KernelEx-only name at load time.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile

from check_pe98 import check as check_wrapper


def imports(pe: pefile.PE) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
        library = descriptor.dll.decode("ascii").upper()
        result[library] = {
            entry.name.decode("ascii") for entry in descriptor.imports
            if entry.name is not None
        }
    return result


def check_guest_exe(path: Path, native: set[str], static_import: bool) -> list[str]:
    issues: list[str] = []
    probe = pefile.PE(str(path))
    optional = probe.OPTIONAL_HEADER
    if probe.FILE_HEADER.Machine != 0x14C or optional.Magic != 0x10B:
        issues.append(f"{path.name} is not PE32 i386")
    if optional.Subsystem != 3 or (
        optional.MajorSubsystemVersion,
        optional.MinorSubsystemVersion,
    ) != (4, 10):
        issues.append(f"{path.name} is not console subsystem 4.10")
    if optional.DllCharacteristics & (0x0040 | 0x0100):
        issues.append(f"{path.name} carries ASLR/NX flags")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if optional.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"{path.name} has unsupported {label}")
    probe_imports = imports(probe)
    if set(probe_imports) != {"KERNEL32.DLL"}:
        issues.append(f"{path.name} DLL set: {set(probe_imports)}")
    else:
        names = probe_imports["KERNEL32.DLL"]
        if static_import and "GetFinalPathNameByHandleW" not in names:
            issues.append("GetFinalPathNameByHandleW is not a static KERNEL32 import")
        if not static_import and "GetFinalPathNameByHandleW" in names:
            issues.append("direct probe unexpectedly imports the KernelEx-only name")
        allowed = native | ({"GetFinalPathNameByHandleW"} if static_import else set())
        for name in sorted(names - allowed):
            issues.append(f"{path.name} imports unverified Win98 OEM symbol {name}")
    probe.close()
    return issues


def check(dll_path: Path, direct_path: Path, probe_path: Path,
          manifest_path: Path) -> list[str]:
    issues = check_wrapper(dll_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    native = set(manifest["dlls"]["KERNEL32.DLL"])

    dll = pefile.PE(str(dll_path))
    for library, names in imports(dll).items():
        if library != "KERNEL32.DLL":
            issues.append(f"final-path wrapper imports {library}")
        for name in sorted(names - native):
            issues.append(f"{name} absent from pinned Win98 OEM KERNEL32")
    dll.close()

    issues.extend(check_guest_exe(direct_path, native, False))
    issues.extend(check_guest_exe(probe_path, native, True))
    return issues


def main() -> int:
    dll_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/finalpath/m98wrap.dll")
    direct_path = Path("build/finalpath/finalpath_smoke.exe")
    probe_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("build/finalpath/finalpath_import_probe.exe")
    manifest_path = Path("benchmarks/win98se-ko-oem-native-exports-v1.json")
    try:
        issues = check(dll_path, direct_path, probe_path, manifest_path)
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: final-path wrapper static gate: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: GetFinalPathNameByHandleW Win98 PE/import static gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
