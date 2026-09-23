"""PE32/Win98 and OEM-native import gate for the GDI alpha provider."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[1]
NAMES = {"GdiAlphaBlend", "GdiGradientFill", "GdiTransparentBlt"}


def check(path: Path, native: dict[str, list[str]]) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path))
    opt = pe.OPTIONAL_HEADER
    is_dll = path.suffix.lower() == ".dll"
    is_static = path.name.lower() in {"gdi_alpha_import_probe.exe",
                                      "gdi_alpha_static_contract.exe"}
    is_contract = path.name.lower() in {"gdi_alpha_direct_contract.exe",
                                        "gdi_alpha_static_contract.exe"}
    if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B or pe.is_dll() != is_dll:
        issues.append("expected i386 PE32 image kind")
    if opt.Subsystem != (2 if is_dll else 3) or (opt.MajorSubsystemVersion,
                              opt.MinorSubsystemVersion) != (4, 10):
        issues.append("expected Win98 subsystem 4.10")
    if opt.DllCharacteristics & (0x40 | 0x100):
        issues.append("unsupported ASLR/NX flags")
    if is_dll and (pe.FILE_HEADER.Characteristics & 0x0001 or
                   not opt.DATA_DIRECTORY[5].VirtualAddress):
        issues.append("DLL lacks base relocations")
    if any(opt.DATA_DIRECTORY[n].VirtualAddress for n in (9, 13, 14)):
        issues.append("TLS/delay/CLR directory present")
    if is_dll:
        exports = {entry.name.decode("ascii") for entry in
                   getattr(pe, "DIRECTORY_ENTRY_EXPORT").symbols if entry.name}
        if exports != {"get_api_table"}:
            issues.append(f"wrong provider exports: {sorted(exports)}")
    imports: dict[str, set[str]] = {}
    for desc in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        dll = desc.dll.decode("ascii").upper()
        if dll != "KERNEL32.DLL" and not ((is_static or is_contract) and
                                            dll == "GDI32.DLL"):
            issues.append(f"unexpected DLL import: {dll}")
        entries = imports.setdefault(dll, set())
        for imp in desc.imports:
            if imp.name is None:
                issues.append(f"ordinal import from {dll}")
                continue
            name = imp.name.decode("ascii")
            entries.add(name)
            if name not in native.get(dll, ()) and not (is_static and
                                                        dll == "GDI32.DLL" and
                                                        name in NAMES):
                issues.append(f"non-OEM import: {dll}!{name}")
    if not imports:
        issues.append("expected Kernel32 native imports")
    if is_static and not NAMES <= imports.get("GDI32.DLL", set()):
        issues.append(f"static GDI32 imports missing: {sorted(NAMES - imports.get('GDI32.DLL', set()))}")
    if not is_static and NAMES & imports.get("GDI32.DLL", set()):
        issues.append("image statically imports routed aliases")
    pe.close()
    return [f"{path.name}: {message}" for message in issues]


def main() -> int:
    manifest = json.loads((ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json")
                          .read_text(encoding="utf-8"))["dlls"]
    paths = [Path(p) for p in sys.argv[1:]] or [ROOT / "build/gdi-alpha/M98GDI.DLL",
                                                  ROOT / "build/gdi-alpha/GDIFIX.DLL",
                                                  ROOT / "build/gdi-alpha/gdi_alpha_host.exe",
                                                  ROOT / "build/gdi-alpha/gdi_alpha_import_probe.exe",
                                                  ROOT / "build/gdi-alpha/gdi_alpha_lifetime.exe",
                                                  ROOT / "build/gdi-alpha/gdi_alpha_direct_contract.exe",
                                                  ROOT / "build/gdi-alpha/gdi_alpha_static_contract.exe"]
    issues = [issue for path in paths for issue in check(path, manifest)]
    if issues:
        for issue in issues:
            print("FAIL:", issue, file=sys.stderr)
        return 1
    print("PASS: GDI alpha provider i386 PE32 Win98 4.10; original Win98 Kernel32 imports only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
