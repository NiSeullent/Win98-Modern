"""Static Windows 98 loader gate for app-local DWMAPI.DLL."""

from __future__ import annotations

import sys
from pathlib import Path

import pefile


EXPECTED_EXPORTS = {"DwmGetColorizationColor", "DwmSetWindowAttribute"}


def check(path: Path) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path), fast_load=False)
    header = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B or not pe.is_dll():
        issues.append("expected a 32-bit x86 DLL")
    if header.Subsystem != 2 or (
        header.MajorSubsystemVersion,
        header.MinorSubsystemVersion,
    ) != (4, 10):
        issues.append("expected Windows GUI subsystem 4.10")
    if header.DllCharacteristics & (0x0040 | 0x0100):
        issues.append("ASLR/NX image flags are unsupported by Windows 98")
    if pe.FILE_HEADER.Characteristics & 0x0001 or not header.DATA_DIRECTORY[5].VirtualAddress:
        issues.append("DLL must have base relocations for app-local load order")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if header.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"unexpected {label} directory")
    if getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        imports = [
            descriptor.dll.decode("ascii", errors="replace")
            for descriptor in pe.DIRECTORY_ENTRY_IMPORT
        ]
        issues.append(f"unexpected DLL imports: {imports}")

    exports = {
        symbol.name.decode("ascii", errors="replace")
        for symbol in getattr(pe, "DIRECTORY_ENTRY_EXPORT", ()).symbols
        if symbol.name is not None
    } if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else set()
    if exports != EXPECTED_EXPORTS:
        issues.append(f"expected {sorted(EXPECTED_EXPORTS)}, got {sorted(exports)}")
    pe.close()
    return issues


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else Path("build/dwmapi.dll")
    try:
        issues = check(path)
    except (OSError, pefile.PEFormatError) as exc:
        print(f"FAIL: DWMAPI PE98 inspection: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: DWMAPI PE32 Win98 loader compatibility gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
