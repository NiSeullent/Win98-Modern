"""Original Win98 PE/import gate for the independent USER32 clipboard probes."""
import json
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[1]
NEW = {"AddClipboardFormatListener", "RemoveClipboardFormatListener",
       "GetUpdatedClipboardFormats"}


def check(path: Path, kind: str, manifest: dict) -> list[str]:
    errors = []
    pe = pefile.PE(str(path))
    opt = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B:
        errors.append("expected i386 PE32")
    if pe.is_dll() != (kind in ("fixture", "provider")):
        errors.append("wrong DLL/EXE image kind")
    subsystem = 2 if kind in ("fixture", "provider") else 3
    if opt.Subsystem != subsystem or (opt.MajorSubsystemVersion,
                                      opt.MinorSubsystemVersion) != (4, 10):
        errors.append("expected Windows 98 subsystem 4.10")
    if opt.DllCharacteristics & (0x40 | 0x100):
        errors.append("ASLR/NX enabled")
    if any(opt.DATA_DIRECTORY[index].VirtualAddress for index in (9, 13, 14)):
        errors.append("TLS/delay import/CLR directory present")
    imports: dict[str, set[str]] = {}
    for desc in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        dll = desc.dll.decode("ascii").upper()
        names = imports.setdefault(dll, set())
        for entry in desc.imports:
            if entry.name is None:
                errors.append(f"ordinal import from {dll}")
            else:
                names.add(entry.name.decode("ascii"))
    if not imports or set(imports) - {"KERNEL32.DLL", "USER32.DLL"}:
        errors.append(f"unexpected import DLLs: {sorted(imports)}")
    for dll, names in imports.items():
        allowed = set(manifest["dlls"].get(dll, ()))
        if kind in ("static", "static-suite") and dll == "USER32.DLL":
            allowed |= NEW
        excess = names - allowed
        if excess:
            errors.append(f"non-OEM imports from {dll}: {sorted(excess)}")
    if kind in ("static", "static-suite") and not NEW <= imports.get("USER32.DLL", set()):
        errors.append(f"missing static USER32 imports: {sorted(NEW - imports.get('USER32.DLL', set()))}")
    if kind not in ("static", "static-suite") and NEW & imports.get("USER32.DLL", set()):
        errors.append("fixture/read-only smoke imports Vista clipboard APIs")
    if kind in ("fixture", "provider"):
        exports = {item.name.decode("ascii") for item in
                   getattr(pe, "DIRECTORY_ENTRY_EXPORT").symbols if item.name}
        if "get_api_table" not in exports:
            errors.append("fixture lacks get_api_table export")
    pe.close()
    return [f"{path.name}: {error}" for error in errors]


def main() -> int:
    paths = [(ROOT / "build/clipboard-tests/clipboard_smoke.exe", "smoke"),
             (ROOT / "build/clipboard-tests/clipboard_import_probe.exe", "static"),
             (ROOT / "build/clipboard-tests/clipboard_static_suite.exe", "static-suite")]
    if len(sys.argv) > 1:
        paths.append((Path(sys.argv[1]), "fixture"))
    if len(sys.argv) > 2:
        paths.append((Path(sys.argv[2]), "provider"))
    manifest = json.loads((ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json")
                          .read_text(encoding="utf-8"))
    errors = []
    for path, kind in paths:
        errors.extend(check(path, kind, manifest))
    if errors:
        for error in errors:
            print("FAIL:", error, file=sys.stderr)
        return 1
    print("PASS: clipboard i386 PE32/Win98 4.10, native-only fixture/smoke, three static USER32 imports in both static probes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
