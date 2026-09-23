"""Gate the app-local no-theme UXTHEME bridge for the Win98 SE loader."""

from __future__ import annotations

import json
import hashlib
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
NPP = ROOT / "benchmarks/media/npp-8.9.8/notepad++.exe"
NPP_SHA256 = "960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78"
NATIVE = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"


def export_names(path: Path) -> set[str]:
    pe = pefile.PE(str(path))
    try:
        return {
            symbol.name.decode("ascii")
            for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols
            if symbol.name
        }
    finally:
        pe.close()


def npp_theme_imports() -> set[str]:
    if hashlib.sha256(NPP.read_bytes()).hexdigest() != NPP_SHA256:
        raise AssertionError("Notepad++ executable differs from pinned 8.9.8 image")
    pe = pefile.PE(str(NPP))
    try:
        matches = [
            descriptor
            for descriptor in pe.DIRECTORY_ENTRY_IMPORT
            if descriptor.dll.upper() == b"UXTHEME.DLL"
        ]
        if len(matches) != 1:
            raise AssertionError("pinned Notepad++ UXTHEME import descriptor missing")
        return {entry.name.decode("ascii") for entry in matches[0].imports if entry.name}
    finally:
        pe.close()


def check(path: Path) -> list[str]:
    problems: list[str] = []
    pe = pefile.PE(str(path))
    try:
        opt = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B or not pe.is_dll():
            problems.append("expected PE32 i386 DLL")
        if opt.Subsystem != 2 or (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
            problems.append("expected GUI subsystem 4.10")
        if opt.DllCharacteristics & (0x0040 | 0x0100):
            problems.append("ASLR/NX flags unsupported on Win98")
        if pe.FILE_HEADER.Characteristics & 0x0001 or not opt.DATA_DIRECTORY[5].VirtualAddress:
            problems.append("relocation directory required for multiple app-local DLLs")
        for index, label in ((9, "TLS"), (13, "delay import"), (14, "CLR")):
            if opt.DATA_DIRECTORY[index].VirtualAddress:
                problems.append(f"unexpected {label} directory")

        exports = export_names(path)
        needed = npp_theme_imports()
        missing = needed - exports
        if missing:
            problems.append(f"missing Notepad++ UXTHEME imports: {sorted(missing)}")
        expected = needed | {"IsThemeActive", "IsAppThemed", "GetWindowTheme"}
        if exports != expected:
            problems.append(f"unexpected bridge export set: {sorted(exports ^ expected)}")

        native = json.loads(NATIVE.read_text(encoding="utf-8"))["dlls"]
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
            module = descriptor.dll.decode("ascii").upper()
            if module not in {"KERNEL32.DLL", "USER32.DLL", "GDI32.DLL"}:
                problems.append(f"unexpected native dependency: {module}")
                continue
            not_native = {
                entry.name.decode("ascii") if entry.name else f"#{entry.ordinal}"
                for entry in descriptor.imports
            } - set(native[module])
            if not_native:
                problems.append(f"{module} imports missing from Win98 OEM: {sorted(not_native)}")
    finally:
        pe.close()
    return problems


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/m98uxtheme.dll"
    try:
        problems = check(path)
    except (OSError, ValueError, pefile.PEFormatError, AttributeError, AssertionError) as exc:
        print(f"FAIL: UXTHEME PE98 inspection: {exc}", file=sys.stderr)
        return 1
    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    if problems:
        return 1
    print("PASS: UXTHEME bridge PE32 4.10, native imports and all pinned NPP theme imports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
