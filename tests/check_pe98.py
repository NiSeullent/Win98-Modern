"""Reject DLL builds that cannot load on a stock Windows 98 SE loader.

This is a static gate, not a substitute for a guest LoadLibrary/KernelEx test.
Only m98wrap.dll is checked: import_probe.exe intentionally imports newer APIs.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pefile


# Verified against KERNEL32.DLL extracted from the supplied Korean Win98 SE OEM
# ISO (WIN98_30.CAB; SHA-256 6771ab74633e9de1359864bd4306a2ed669bec50de7ac9bb9978c05bf04563ca).
# Recheck this list whenever a new native import is added to m98wrap.dll.
NATIVE_KERNEL32 = frozenset(
    {
        "DeleteCriticalSection",
        "EnterCriticalSection",
        "GetExitCodeProcess",
        "GetFileInformationByHandle",
        "GetLastError",
        "GetSystemTimeAsFileTime",
        "GetTickCount",
        "GlobalMemoryStatus",
        "InitializeCriticalSection",
        "LeaveCriticalSection",
        "SetLastError",
        "Sleep",
    }
)


def check(path: Path) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path), fast_load=False)
    header = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B:
        issues.append("expected a 32-bit x86 PE image")
    if not pe.is_dll():
        issues.append("image is not a DLL")
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
    if not imports:
        issues.append("DLL has no imports; loader verification is inconclusive")

    exported = {
        symbol.name.decode("ascii", errors="replace")
        for symbol in getattr(pe, "DIRECTORY_ENTRY_EXPORT", ()).symbols
        if symbol.name is not None
    } if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else set()
    if "get_api_table" not in exported:
        issues.append("KernelEx get_api_table export is absent")
    pe.close()
    return issues


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else Path("build/m98wrap.dll")
    try:
        issues = check(path)
    except (OSError, pefile.PEFormatError) as exc:
        print(f"FAIL: PE98 inspection: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: PE32 Win98 loader compatibility gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
