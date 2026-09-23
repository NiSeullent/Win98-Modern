"""Static PE/import gate for the three application-restart KernelEx names."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile

from check_pe98 import check as check_wrapper


RESTART_NAMES = {
    "GetApplicationRestartSettings",
    "RegisterApplicationRestart",
    "UnregisterApplicationRestart",
}


def imports(pe: pefile.PE) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
        library = descriptor.dll.decode("ascii").upper()
        names: set[str] = set()
        for entry in descriptor.imports:
            if entry.name is None:
                raise ValueError(f"ordinal import from {library}")
            names.add(entry.name.decode("ascii"))
        result[library] = names
    return result


def check_exe(path: Path, native: set[str], static_import: bool) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path))
    optional = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or optional.Magic != 0x10B:
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
    imported = imports(pe)
    if set(imported) != {"KERNEL32.DLL"}:
        issues.append(f"{path.name} DLL set: {set(imported)}")
    else:
        names = imported["KERNEL32.DLL"]
        actual = names & RESTART_NAMES
        expected = RESTART_NAMES if static_import else set()
        if actual != expected:
            issues.append(
                f"{path.name} restart imports {sorted(actual)}, expected {sorted(expected)}"
            )
        permitted = native | (RESTART_NAMES if static_import else set())
        for name in sorted(names - permitted):
            issues.append(f"{path.name} imports unverified Win98 symbol {name}")
    pe.close()
    return issues


def check(dll_path: Path, direct_path: Path, static_path: Path,
          manifest_path: Path) -> list[str]:
    issues = check_wrapper(dll_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    native = set(manifest["dlls"]["KERNEL32.DLL"])
    dll = pefile.PE(str(dll_path))
    for library, names in imports(dll).items():
        if library != "KERNEL32.DLL":
            issues.append(f"wrapper imports {library}")
        for name in sorted(names - native):
            issues.append(f"{name} absent from pinned Win98 OEM KERNEL32")
    dll.close()
    issues.extend(check_exe(direct_path, native, False))
    issues.extend(check_exe(static_path, native, True))
    return issues


def main() -> int:
    base = Path("build/restart")
    try:
        issues = check(
            base / "m98wrap.dll",
            base / "restart_smoke.exe",
            base / "restart_import_probe.exe",
            Path("benchmarks/win98se-ko-oem-native-exports-v1.json"),
        )
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: restart PE gate: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: application-restart Win98 PE/import static gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
