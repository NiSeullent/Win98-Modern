"""Gate the static-import system-font probe before a Windows 98 guest run."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"


def check(path: Path) -> list[str]:
    problems: list[str] = []
    native = json.loads(NATIVE.read_text(encoding="utf-8"))["dlls"]
    with pefile.PE(str(path)) as pe:
        opt = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B or pe.is_dll():
            problems.append("expected PE32 i386 executable")
        if opt.Subsystem != 3 or (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
            problems.append("expected console subsystem 4.10")
        if opt.DllCharacteristics & (0x0040 | 0x0100):
            problems.append("unsupported ASLR/NX flags")
        for index, label in ((9, "TLS"), (13, "delay import"), (14, "CLR")):
            if opt.DATA_DIRECTORY[index].VirtualAddress:
                problems.append(f"unexpected {label} directory")

        imports = {
            descriptor.dll.decode("ascii").upper(): {
                entry.name.decode("ascii") if entry.name else f"#{entry.ordinal}"
                for entry in descriptor.imports
            }
            for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ())
        }
        if imports.get("UXTHEME.DLL") != {"GetThemeSysFont"}:
            problems.append("probe must statically import only UXTHEME!GetThemeSysFont")
        for module, symbols in imports.items():
            if module == "UXTHEME.DLL":
                continue
            if module not in {"KERNEL32.DLL", "USER32.DLL"}:
                problems.append(f"unexpected import module: {module}")
                continue
            absent = symbols - set(native[module])
            if absent:
                problems.append(f"{module} imports absent from Win98 OEM: {sorted(absent)}")
    return problems


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/uxtheme-known/uxtheme_sysfont_guest_probe.exe"
    try:
        problems = check(path)
    except (OSError, ValueError, AttributeError, pefile.PEFormatError) as exc:
        print(f"FAIL: system-font guest probe PE inspection: {exc}", file=sys.stderr)
        return 1
    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    if problems:
        return 1
    print("PASS: static UXTHEME GetThemeSysFont probe, PE32 console 4.10, OEM-native dependencies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
