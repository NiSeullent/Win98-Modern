"""Verify the KernelEx SHELL32 API library uses only original Win98 exports."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


EXPECTED_IMPORTS = {
    "KERNEL32.DLL": {
        "GetFileAttributesA", "GetLastError", "GetWindowsDirectoryA",
        "MultiByteToWideChar", "SetLastError", "lstrcatA", "lstrcpyA",
        "lstrlenA",
    },
    "OLE32.DLL": {"CoTaskMemAlloc", "CoTaskMemFree"},
    "SHELL32.DLL": {"SHGetDesktopFolder", "SHGetPathFromIDListA", "ShellExecuteExA"},
}
EXPECTED_PROBE_IMPORTS = {
    "SHELL32.DLL": {
        "SHCreateItemFromParsingName", "SHOpenFolderAndSelectItems",
        "SHParseDisplayName",
    },
    "OLE32.DLL": {"CoInitialize", "CoTaskMemFree", "CoUninitialize"},
    "KERNEL32.DLL": {
        "ExitProcess", "GetModuleFileNameA", "GetStdHandle",
        "MultiByteToWideChar", "WriteFile",
    },
}


def check(path: Path) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path), fast_load=False)
    header = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B or not pe.is_dll():
        issues.append("expected PE32 x86 DLL")
    if header.Subsystem != 2 or (
        header.MajorSubsystemVersion,
        header.MinorSubsystemVersion,
    ) != (4, 10):
        issues.append("expected Windows GUI subsystem 4.10")
    if header.DllCharacteristics & (0x0040 | 0x0100):
        issues.append("ASLR/NX image flags unsupported by Windows 98")
    if not header.DATA_DIRECTORY[5].VirtualAddress or pe.FILE_HEADER.Characteristics & 0x0001:
        issues.append("DLL lacks base relocations needed when its preferred base is occupied")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if header.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"unexpected {label} directory")

    native = json.loads(
        (Path(__file__).resolve().parents[1] / "benchmarks/win98se-ko-oem-native-exports-v1.json").read_text(encoding="utf-8")
    )["dlls"]
    imports: dict[str, set[str]] = {}
    for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        library = descriptor.dll.decode("ascii", errors="replace").upper()
        names = imports.setdefault(library, set())
        for entry in descriptor.imports:
            if entry.name is None:
                issues.append(f"ordinal import from {library}: {entry.ordinal}")
                continue
            name = entry.name.decode("ascii", errors="replace")
            names.add(name)
            if name not in native.get(library, ()):
                issues.append(f"not an original Win98 export: {library}!{name}")
    if imports != EXPECTED_IMPORTS:
        issues.append(f"unexpected import map: {imports}")

    exports = (
        {
            symbol.name.decode("ascii", errors="replace")
            for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols
            if symbol.name is not None
        }
        if hasattr(pe, "DIRECTORY_ENTRY_EXPORT")
        else set()
    )
    if exports != {"get_api_table"}:
        issues.append(f"expected KernelEx get_api_table only, got {sorted(exports)}")
    pe.close()
    return issues


def check_probe(path: Path) -> list[str]:
    """The guest probe must require the new named SHELL32 export at load time."""
    issues: list[str] = []
    pe = pefile.PE(str(path), fast_load=False)
    header = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B or pe.is_dll():
        issues.append("shell probe must be a PE32 x86 executable")
    if header.Subsystem != 3 or (
        header.MajorSubsystemVersion,
        header.MinorSubsystemVersion,
    ) != (4, 10):
        issues.append("shell probe must be a Windows console subsystem 4.10 executable")
    if header.DllCharacteristics & (0x0040 | 0x0100):
        issues.append("shell probe has Windows 98 unsupported ASLR/NX flags")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if header.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"shell probe has unexpected {label} directory")
    imports: dict[str, set[str]] = {}
    for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        library = descriptor.dll.decode("ascii", errors="replace").upper()
        names = imports.setdefault(library, set())
        for entry in descriptor.imports:
            if entry.name is None:
                issues.append(f"shell probe has ordinal import from {library}: {entry.ordinal}")
            else:
                names.add(entry.name.decode("ascii", errors="replace"))
    if imports != EXPECTED_PROBE_IMPORTS:
        issues.append(f"unexpected static Shell probe import map: {imports}")
    native = json.loads(
        (Path(__file__).resolve().parents[1] / "benchmarks/win98se-ko-oem-native-exports-v1.json").read_text(encoding="utf-8")
    )["dlls"]
    for library, names in imports.items():
        for name in names:
            if library == "SHELL32.DLL" and name in (
                "SHCreateItemFromParsingName", "SHOpenFolderAndSelectItems",
                "SHParseDisplayName"
            ):
                continue
            if name not in native.get(library, ()):
                issues.append(f"probe import is not original Win98 API: {library}!{name}")
    pe.close()
    return issues


def main() -> int:
    if len(sys.argv) > 3:
        print("usage: check_m98shell_pe98.py [m98shell.dll [shell_import_probe.exe]]", file=sys.stderr)
        return 2
    path = Path(sys.argv[1]) if len(sys.argv) >= 2 else Path("build/m98shell.dll")
    try:
        issues = check(path)
        if len(sys.argv) == 3:
            issues.extend(check_probe(Path(sys.argv[2])))
    except (OSError, pefile.PEFormatError, ValueError) as exc:
        print(f"FAIL: m98shell PE inspection: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: m98shell PE32 Win98 loader/native-import gate" +
          ("; static SHELL32 import probe" if len(sys.argv) == 3 else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
