"""Validate the redistributable KernelEx UXTHEME KnownDLL without app files.

The pinned Notepad++ executable is not redistributed. Its 16 static import
names are frozen here from the SHA-256-pinned source analysis in
docs/UXTHEME_NPP_PORT.md. The source checkout has a stronger target-exe gate.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DEF = ROOT / "third_party/KernelEx/auxiliary/uxtheme/uxtheme.def"
NATIVE = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"
ADDITIONS = {
    "BeginBufferedAnimation", "BufferedPaintRenderAnimation",
    "BufferedPaintStopAllAnimations", "DrawThemeTextEx",
    "EndBufferedAnimation", "GetThemeTransitionDuration",
}
REPLACED = {
    "CloseThemeData", "DrawThemeBackground", "DrawThemeParentBackground",
    "EnableThemeDialogTexture", "GetThemeBackgroundContentRect",
    "GetThemeColor", "GetThemeFont", "GetThemePartSize",
    "OpenThemeData", "SetWindowTheme", "GetThemeSysFont",
}
NPP_UXTHEME_IMPORTS = {
    "BeginBufferedAnimation", "BufferedPaintRenderAnimation",
    "BufferedPaintStopAllAnimations", "CloseThemeData",
    "DrawThemeBackground", "DrawThemeParentBackground", "DrawThemeTextEx",
    "EnableThemeDialogTexture", "EndBufferedAnimation",
    "GetThemeBackgroundContentRect", "GetThemeColor", "GetThemeFont",
    "GetThemePartSize", "GetThemeTransitionDuration",
    "OpenThemeData", "SetWindowTheme",
}


def names_from_def(path: Path) -> dict[str, str]:
    lines = path.read_text(encoding="ascii").splitlines()
    try:
        start = lines.index("EXPORTS") + 1
    except ValueError as exc:
        raise ValueError(f"missing EXPORTS in {path}") from exc
    result: dict[str, str] = {}
    for line in lines[start:]:
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        name, sep, target = line.partition("=")
        result[name] = target if sep else name
    return result


def check(path: Path) -> list[str]:
    problems: list[str] = []
    original = names_from_def(SOURCE_DEF)
    generated = names_from_def(path.with_name("uxtheme-known.def"))
    expected = set(original) | ADDITIONS
    if set(generated) != expected:
        problems.append(f"generated DEF export difference: {sorted(set(generated) ^ expected)}")
    for name in expected:
        expected_target = f"m98_{name}" if name in REPLACED | ADDITIONS else name
        if generated.get(name) != expected_target:
            problems.append(f"wrong DEF route for {name}: {generated.get(name)}")

    native = json.loads(NATIVE.read_text(encoding="utf-8"))["dlls"]
    with pefile.PE(str(path)) as pe:
        header = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B or not pe.is_dll():
            problems.append("expected PE32 i386 DLL")
        if header.Subsystem != 2 or (
            header.MajorSubsystemVersion, header.MinorSubsystemVersion
        ) != (4, 10):
            problems.append("expected GUI subsystem 4.10")
        if header.DllCharacteristics & (0x0040 | 0x0100):
            problems.append("ASLR/NX flags unsupported on Win98")
        if pe.FILE_HEADER.Characteristics & 1 or not header.DATA_DIRECTORY[5].VirtualAddress:
            problems.append("base relocations required")
        for index, label in ((9, "TLS"), (13, "delay import"), (14, "CLR")):
            if header.DATA_DIRECTORY[index].VirtualAddress:
                problems.append(f"unexpected {label} directory")
        actual = {
            item.name.decode("ascii") for item in pe.DIRECTORY_ENTRY_EXPORT.symbols
            if item.name
        }
        if actual != expected:
            problems.append(f"PE export difference: {sorted(actual ^ expected)}")
        if not NPP_UXTHEME_IMPORTS <= actual:
            problems.append(f"missing pinned NPP imports: {sorted(NPP_UXTHEME_IMPORTS - actual)}")
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
            library = descriptor.dll.decode("ascii").upper()
            if library not in {"KERNEL32.DLL", "USER32.DLL", "GDI32.DLL"}:
                problems.append(f"unexpected native dependency {library}")
                continue
            missing = {
                item.name.decode("ascii") if item.name else f"#{item.ordinal}"
                for item in descriptor.imports
            } - set(native.get(library, ()))
            if missing:
                problems.append(f"{library} imports absent from Win98 OEM: {sorted(missing)}")
    return problems


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/uxtheme-known/UXTHEME.DLL"
    try:
        problems = check(path)
    except (OSError, ValueError, AttributeError, pefile.PEFormatError) as exc:
        print(f"FAIL: redistributable UXTHEME PE inspection: {exc}", file=sys.stderr)
        return 1
    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    if problems:
        return 1
    print("PASS: redistributable UXTHEME PE32 4.10, 54 exports, 16 pinned NPP imports and OEM-native dependencies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
