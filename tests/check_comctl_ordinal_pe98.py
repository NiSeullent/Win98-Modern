"""Static PE32/Win98 and original-media-import gate for COMCTL32 ordinals."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[1]


def check(path: Path, native: dict[str, list[str]]) -> list[str]:
    pe = pefile.PE(str(path))
    errors: list[str] = []
    opt = pe.OPTIONAL_HEADER
    dll = path.suffix.lower() == ".dll"
    ordinal_probe = path.name.lower() == "comctl_ordinal_import_probe.exe"
    if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B or pe.is_dll() != dll:
        errors.append("expected i386 PE32 image")
    if opt.Subsystem != (2 if dll else 3) or (opt.MajorSubsystemVersion,
                                              opt.MinorSubsystemVersion) != (4, 10):
        errors.append("expected Win98 subsystem 4.10")
    if opt.DllCharacteristics & (0x40 | 0x100):
        errors.append("unsupported ASLR/NX flag")
    if dll and not opt.DATA_DIRECTORY[5].VirtualAddress:
        errors.append("DLL has no relocation directory")
    if any(opt.DATA_DIRECTORY[n].VirtualAddress for n in (9, 13, 14)):
        errors.append("TLS, delay import or CLR directory present")
    directory = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    exports = {entry.name.decode("ascii") for entry in directory.symbols
               if entry.name} if directory else set()
    if dll and exports != {"get_api_table"}:
        errors.append(f"unexpected exports: {exports}")
    imports = {}
    for desc in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        target = desc.dll.decode("ascii").upper()
        imports[target] = set()
        if target not in {"KERNEL32.DLL", "USER32.DLL", "GDI32.DLL"} and not (
                ordinal_probe and target == "COMCTL32.DLL"):
            errors.append(f"unexpected dependency {target}")
        if dll and target == "GDI32.DLL" and path.name.lower() != "m98ctlp.dll":
            errors.append("provider unexpectedly imports GDI32")
        for item in desc.imports:
            name = item.name.decode("ascii") if item.name else None
            if ordinal_probe and target == "COMCTL32.DLL":
                if name or item.ordinal not in {345, 381}:
                    errors.append(f"wrong COMCTL32 import: {name or item.ordinal}")
                imports[target].add(item.ordinal)
                continue
            if not name or name not in native.get(target, ()):
                errors.append(f"non-OEM import {target}!{name or '#' + str(item.ordinal)}")
            imports[target].add(name)
    if not {"KERNEL32.DLL", "USER32.DLL"} <= imports.keys():
        errors.append("expected original KERNEL32 and USER32 imports")
    if ordinal_probe and imports.get("COMCTL32.DLL") != {345, 381}:
        errors.append("expected COMCTL32 ordinal imports 345 and 381")
    pe.close()
    return [f"{path.name}: {item}" for item in errors]


def main() -> int:
    native = json.loads((ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json")
                        .read_text(encoding="utf-8"))["dlls"]
    paths = [Path(s) for s in sys.argv[1:]]
    if not paths:
        paths = [ROOT / "build/comctl-ord/M98CTL.DLL",
                 ROOT / "build/comctl-ord/comctl_ordinal_host.exe",
                 ROOT / "build/comctl-ord/comctl_ordinal_import_probe.exe"]
    issues = [issue for path in paths for issue in check(path, native)]
    if issues:
        for issue in issues:
            print("FAIL:", issue, file=sys.stderr)
        return 1
    print("PASS: COMCTL32 provider i386 PE32 Win98 4.10 with OEM imports only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
