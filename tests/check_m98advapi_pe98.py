"""Static Win98 loader and original-media import gate for m98advapi.dll."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"
EXPECTED = {
    "ADVAPI32.DLL": {"RegOpenKeyExA", "RegQueryValueExA", "RegCloseKey"},
    "KERNEL32.DLL": {
        "ExpandEnvironmentStringsA", "GetLastError", "GetProcessHeap",
        "HeapAlloc", "HeapFree", "MultiByteToWideChar", "WideCharToMultiByte",
    },
}


def check(path: Path, import_probe: Path, direct_smoke: Path) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path), fast_load=False)
    header = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B or not pe.is_dll():
        issues.append("expected a PE32 x86 DLL")
    if header.Subsystem != 2 or (
        header.MajorSubsystemVersion, header.MinorSubsystemVersion
    ) != (4, 10):
        issues.append("expected Win98 GUI subsystem 4.10")
    if header.DllCharacteristics & (0x0040 | 0x0100):
        issues.append("ASLR/NX image flags are unsupported by Win98")
    if pe.FILE_HEADER.Characteristics & 0x0001 or not header.DATA_DIRECTORY[5].VirtualAddress:
        issues.append("DLL must contain base relocations")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if header.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"unexpected {label} directory")

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))["dlls"]
    found: dict[str, set[str]] = {}
    for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        library = descriptor.dll.decode("ascii", errors="replace").upper()
        if library not in EXPECTED:
            issues.append(f"unexpected native library: {library}")
        names: set[str] = set()
        for entry in descriptor.imports:
            if entry.name is None:
                issues.append(f"ordinal import from {library}: {entry.ordinal}")
                continue
            name = entry.name.decode("ascii", errors="replace")
            names.add(name)
            if name not in manifest.get(library, []):
                issues.append(f"{library}!{name} absent from OEM ISO manifest")
        found[library] = names
    for library, required in EXPECTED.items():
        extra = found.get(library, set()) - required
        missing = required - found.get(library, set())
        if extra:
            issues.append(f"unexpected {library} imports: {sorted(extra)}")
        if missing:
            issues.append(f"missing expected {library} imports: {sorted(missing)}")
    exports = {
        symbol.name.decode("ascii", errors="replace")
        for symbol in getattr(pe, "DIRECTORY_ENTRY_EXPORT", ()).symbols
        if symbol.name is not None
    } if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else set()
    if exports != {"get_api_table"}:
        issues.append(f"expected only KernelEx get_api_table export, got {sorted(exports)}")
    pe.close()

    probe_imports: set[tuple[str, str | None]] = set()
    for test_path, is_import_probe in ((direct_smoke, False), (import_probe, True)):
        test_pe = pefile.PE(str(test_path), fast_load=False)
        test_header = test_pe.OPTIONAL_HEADER
        if test_header.Subsystem != 3 or (
            test_header.MajorSubsystemVersion, test_header.MinorSubsystemVersion
        ) != (4, 10):
            issues.append(f"{test_path.name} is not Win98 console subsystem 4.10")
        for descriptor in getattr(test_pe, "DIRECTORY_ENTRY_IMPORT", ()):
            library = descriptor.dll.decode("ascii", errors="replace").upper()
            if library not in manifest:
                issues.append(f"{test_path.name} has unknown library {library}")
            for entry in descriptor.imports:
                name = entry.name.decode("ascii", errors="replace") if entry.name else None
                if is_import_probe:
                    probe_imports.add((library, name))
                if library == "ADVAPI32.DLL" and name in {"RegGetValueA", "RegGetValueW"} and is_import_probe:
                    continue
                if name is None or name not in manifest.get(library, []):
                    issues.append(f"{test_path.name} imports non-OEM {library}!{name}")
        test_pe.close()
    for name in ("RegGetValueA", "RegGetValueW"):
        if ("ADVAPI32.DLL", name) not in probe_imports:
            issues.append(f"static import probe lacks ADVAPI32.DLL!{name}")
    return issues


def main() -> int:
    dll = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/advapi/m98advapi.dll"
    probe = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "build/advapi/advapi_import_probe.exe"
    smoke = Path(sys.argv[3]) if len(sys.argv) > 3 else ROOT / "build/advapi/advapi_smoke.exe"
    try:
        issues = check(dll, probe, smoke)
    except (OSError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: m98advapi PE98 gate: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: m98advapi PE32 OEM-native loader and ADVAPI32 static-import gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
