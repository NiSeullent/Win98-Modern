"""Static Windows 98 loader gate for the app-local DBGHELP.DLL bridge."""

from __future__ import annotations

import sys
from pathlib import Path

import pefile


# Each name is present in the Korean Win98 SE OEM KERNEL32 export manifest.
NATIVE_KERNEL32 = frozenset(
    {
        "FreeLibrary",
        "GetLastError",
        "GetProcAddress",
        "GetSystemDirectoryA",
        "LoadLibraryA",
        "SetLastError",
    }
)


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
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if header.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"unexpected {label} directory")

    imports: set[str] = set()
    for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        library = descriptor.dll.decode("ascii", errors="replace").upper()
        if library != "KERNEL32.DLL":
            issues.append(f"unsupported import library: {library}")
        for entry in descriptor.imports:
            if entry.name is None:
                issues.append(f"ordinal import from {library}: {entry.ordinal}")
                continue
            name = entry.name.decode("ascii", errors="replace")
            imports.add(name)
            if library == "KERNEL32.DLL" and name not in NATIVE_KERNEL32:
                issues.append(f"unverified Win98 KERNEL32 import: {name}")
    if imports != NATIVE_KERNEL32:
        issues.append(f"unexpected import set: {sorted(imports)}")

    exports = {
        symbol.name.decode("ascii", errors="replace")
        for symbol in getattr(pe, "DIRECTORY_ENTRY_EXPORT", ()).symbols
        if symbol.name is not None
    } if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else set()
    if exports != {"ImageNtHeader"}:
        issues.append(f"expected only ImageNtHeader export, got {sorted(exports)}")
    pe.close()
    return issues


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else Path("build/dbghelp.dll")
    try:
        issues = check(path)
    except (OSError, pefile.PEFormatError) as exc:
        print(f"FAIL: DBGHELP PE98 inspection: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: DBGHELP PE32 Win98 loader compatibility gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
