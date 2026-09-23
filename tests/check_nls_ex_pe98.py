"""Static PE/ABI gate for the isolated Win98 NLS Ex bridge fixtures."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "nls-ex"


def inspect(path: Path, *, dll: bool, native: set[str],
            required: set[str], permitted_extra: set[str] = frozenset()) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path))
    optional = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or optional.Magic != 0x10B:
        issues.append(f"{path.name}: expected PE32 i386")
    if pe.is_dll() != dll or optional.Subsystem != (2 if dll else 3):
        issues.append(f"{path.name}: wrong image kind/subsystem")
    if (optional.MajorSubsystemVersion, optional.MinorSubsystemVersion) != (4, 10):
        issues.append(f"{path.name}: expected subsystem 4.10")
    if optional.DllCharacteristics & (0x0040 | 0x0100):
        issues.append(f"{path.name}: ASLR/NX enabled")
    for index, label in ((9, "TLS"), (13, "delay import"), (14, "CLR")):
        if optional.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"{path.name}: unsupported {label}")

    imports: dict[str, set[str]] = {}
    for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        library = descriptor.dll.decode("ascii").upper()
        imports[library] = set()
        for entry in descriptor.imports:
            if entry.name is None:
                issues.append(f"{path.name}: ordinal import from {library}")
            else:
                imports[library].add(entry.name.decode("ascii"))
    if set(imports) != {"KERNEL32.DLL"}:
        issues.append(f"{path.name}: unexpected import DLLs {sorted(imports)}")
    names = imports.get("KERNEL32.DLL", set())
    for name in sorted(required - names):
        issues.append(f"{path.name}: missing import {name}")
    for name in sorted(names - native - permitted_extra):
        issues.append(f"{path.name}: unverified Win98 import {name}")
    if permitted_extra and names & permitted_extra != permitted_extra:
        issues.append(f"{path.name}: missing both static Ex imports")
    if not permitted_extra and names & {"CompareStringEx", "LCMapStringEx"}:
        issues.append(f"{path.name}: unexpected Ex import in direct fixture")
    if dll:
        exports = {
            entry.name.decode("ascii")
            for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols
            if entry.name is not None
        }
        if "get_api_table" not in exports:
            issues.append(f"{path.name}: missing KernelEx get_api_table export")
    pe.close()
    return issues


def main() -> int:
    try:
        manifest = json.loads(
            (ROOT / "benchmarks" / "win98se-ko-oem-native-exports-v1.json")
            .read_text(encoding="utf-8")
        )
        native = set(manifest["dlls"]["KERNEL32.DLL"])
        expected_native = {"LCMapStringW", "CompareStringW"}
        if not expected_native <= native:
            raise ValueError("original Win98 OEM KERNEL32 lacks W NLS exports")
        issues: list[str] = []
        issues += inspect(OUT / "nls_ex_fixture.dll", dll=True,
                          native=native, required=expected_native)
        issues += inspect(OUT / "nls_ex_smoke.exe", dll=False,
                          native=native, required={"LoadLibraryA", "GetProcAddress"})
        issues += inspect(OUT / "nls_ex_integrated_probe.exe", dll=False,
                          native=native, required={"LoadLibraryA", "GetProcAddress"})
        issues += inspect(OUT / "nls_ex_native_probe.exe", dll=False,
                          native=native, required=expected_native)
        issues += inspect(OUT / "nls_ex_import_probe.exe", dll=False,
                          native=native,
                          required={"CompareStringEx", "LCMapStringEx"},
                          permitted_extra={"CompareStringEx", "LCMapStringEx"})
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: NLS Ex PE gate: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: NLS Ex PE32/i386, Win98 loader, OEM native imports, static Ex ABI")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
