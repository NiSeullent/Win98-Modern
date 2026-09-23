"""Static native-Win98 import gate for the GUI startup diagnostic.

An OEM export match does not prove that the guest loader or GUI behavior works.
Only a real Win98 guest execution can establish those facts.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"
REQUIRED = {
    "KERNEL32.DLL": {
        "CreateProcessA",
        "WaitForSingleObject",
        "GetExitCodeProcess",
        "TerminateProcess",
        "GetTickCount",
        "Sleep",
        "WriteFile",
    },
    "USER32.DLL": {
        "EnumWindows",
        "GetWindowThreadProcessId",
        "IsWindowVisible",
        "GetClassNameA",
        "GetWindowTextA",
        "PostMessageA",
    },
}


def check(image_path: Path) -> tuple[list[str], dict[str, set[str]]]:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != "w98mod.export-manifest.v1":
        raise ValueError("native export manifest has an unexpected schema")
    native = {name: set(manifest["dlls"][name]) for name in REQUIRED}
    issues: list[str] = []
    imports = {name: set() for name in REQUIRED}
    pe = pefile.PE(str(image_path), fast_load=False)
    try:
        header = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B:
            issues.append("expected a PE32 x86 image")
        if pe.is_dll():
            issues.append("expected an executable, not a DLL")
        if header.Subsystem != 3 or (
            header.MajorSubsystemVersion,
            header.MinorSubsystemVersion,
        ) != (4, 10):
            issues.append("expected a console subsystem 4.10 image")
        if (header.MajorOperatingSystemVersion, header.MinorOperatingSystemVersion) != (4, 10):
            issues.append("expected minimum operating system version 4.10")
        if header.DllCharacteristics & (0x0040 | 0x0100):
            issues.append("ASLR/NX image flags are unsupported by Windows 98")
        for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
            if header.DATA_DIRECTORY[index].VirtualAddress:
                issues.append(f"unexpected {label} directory")
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
            library = descriptor.dll.decode("ascii", errors="replace").upper()
            if library not in REQUIRED:
                issues.append(f"unsupported import library: {library}")
            for entry in descriptor.imports:
                if entry.name is None:
                    issues.append(f"ordinal import from {library}: {entry.ordinal}")
                    continue
                name = entry.name.decode("ascii", errors="replace")
                if library in native:
                    imports[library].add(name)
                    if name not in native[library]:
                        issues.append(f"absent from native {library} manifest: {name}")
        for library, expected in REQUIRED.items():
            for name in sorted(expected - imports[library]):
                issues.append(f"required diagnostic import missing: {library}!{name}")
    finally:
        pe.close()
    return issues, imports


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else ROOT / "build/app_probe.exe"
    try:
        issues, imports = check(path)
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: app probe PE inspection: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    counts = ", ".join(f"{library}={len(names)}" for library, names in imports.items())
    print(f"PASS: PE32 console 4.10; native OEM imports only ({counts})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
