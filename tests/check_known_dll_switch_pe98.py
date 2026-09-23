"""Check Win98 loader ABI and native imports of the guarded registry helper."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"


def check(path: Path) -> list[str]:
    problems: list[str] = []
    with pefile.PE(str(path)) as pe:
        opt = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B or pe.is_dll():
            problems.append("expected PE32 i386 executable")
        if opt.Subsystem != 3 or (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
            problems.append("expected console subsystem 4.10")
        if opt.DllCharacteristics & (0x0040 | 0x0100):
            problems.append("ASLR/NX flags unsupported on Win98")
        for index, label in ((9, "TLS"), (13, "delay import"), (14, "CLR")):
            if opt.DATA_DIRECTORY[index].VirtualAddress:
                problems.append(f"unexpected {label} directory")
        native = json.loads(NATIVE.read_text(encoding="utf-8"))["dlls"]
        modules = set()
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
            module = descriptor.dll.decode("ascii").upper()
            modules.add(module)
            if module not in {"KERNEL32.DLL", "ADVAPI32.DLL"}:
                problems.append(f"unexpected native dependency: {module}")
                continue
            not_native = {
                item.name.decode("ascii") if item.name else f"#{item.ordinal}"
                for item in descriptor.imports
            } - set(native[module])
            if not_native:
                problems.append(f"{module} imports absent from Win98 OEM: {sorted(not_native)}")
        if modules != {"KERNEL32.DLL", "ADVAPI32.DLL"}:
            problems.append(f"expected KERNEL32 and ADVAPI32 imports: {sorted(modules)}")
    return problems


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/known_dll_switch.exe"
    try:
        problems = check(path)
    except (OSError, ValueError, AttributeError, pefile.PEFormatError) as exc:
        print(f"FAIL: KnownDLL helper PE inspection: {exc}", file=sys.stderr)
        return 1
    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    if problems:
        return 1
    print("PASS: guarded KnownDLL helper PE32 4.10 with OEM-native registry imports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
