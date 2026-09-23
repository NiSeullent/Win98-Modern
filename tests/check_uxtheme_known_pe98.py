"""Check the full KernelEx KnownDLL replacement, without executing Windows code."""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
OLD_DEF = ROOT / "third_party/KernelEx/auxiliary/uxtheme/uxtheme.def"
NPP = ROOT / "benchmarks/media/npp-8.9.8/notepad++.exe"
NPP_SHA256 = "960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78"
NATIVE = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"
ADDITIONS = {
    "BeginBufferedAnimation",
    "BufferedPaintRenderAnimation",
    "BufferedPaintStopAllAnimations",
    "DrawThemeTextEx",
    "EndBufferedAnimation",
    "GetThemeTransitionDuration",
}
REPLACED_STUBS = {
    "CloseThemeData",
    "DrawThemeBackground",
    "DrawThemeParentBackground",
    "EnableThemeDialogTexture",
    "GetThemeBackgroundContentRect",
    "GetThemeColor",
    "GetThemeFont",
    "GetThemePartSize",
    "OpenThemeData",
    "SetWindowTheme",
}
REPLACED_METRICS = {"GetThemeSysFont"}


def original_exports() -> set[str]:
    lines = OLD_DEF.read_text(encoding="ascii").splitlines()
    start = lines.index("EXPORTS")
    return {line.strip().split("=")[0] for line in lines[start + 1 :] if line.strip()}


def npp_imports() -> set[str]:
    if hashlib.sha256(NPP.read_bytes()).hexdigest() != NPP_SHA256:
        raise AssertionError("Pinned Notepad++ executable hash changed")
    with pefile.PE(str(NPP)) as pe:
        descriptors = [
            item for item in pe.DIRECTORY_ENTRY_IMPORT
            if item.dll.upper() == b"UXTHEME.DLL"
        ]
        if len(descriptors) != 1:
            raise AssertionError("Pinned Notepad++ UXTHEME import descriptor missing")
        return {entry.name.decode("ascii") for entry in descriptors[0].imports if entry.name}


def check(path: Path) -> list[str]:
    problems: list[str] = []
    generated_def = path.with_name("uxtheme-known.def")
    definitions = {
        line.strip().split("=", 1)[0]: line.strip().split("=", 1)[-1]
        for line in generated_def.read_text(encoding="ascii").splitlines()[2:]
        if line.strip()
    }
    for name in REPLACED_STUBS | REPLACED_METRICS | ADDITIONS:
        if definitions.get(name) != f"m98_{name}":
            problems.append(f"{name} is not mapped to project implementation")
    for name in original_exports() - REPLACED_STUBS - REPLACED_METRICS:
        if definitions.get(name) != name:
            problems.append(f"{name} upstream mapping unexpectedly changed")
    with pefile.PE(str(path)) as pe:
        opt = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B or not pe.is_dll():
            problems.append("expected PE32 i386 DLL")
        if opt.Subsystem != 2 or (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
            problems.append("expected GUI subsystem 4.10")
        if opt.DllCharacteristics & (0x0040 | 0x0100):
            problems.append("ASLR/NX flags unsupported on Win98")
        if pe.FILE_HEADER.Characteristics & 0x0001 or not opt.DATA_DIRECTORY[5].VirtualAddress:
            problems.append("relocation directory required")
        for index, label in ((9, "TLS"), (13, "delay import"), (14, "CLR")):
            if opt.DATA_DIRECTORY[index].VirtualAddress:
                problems.append(f"unexpected {label} directory")

        actual = {
            symbol.name.decode("ascii")
            for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols
            if symbol.name
        }
        original = original_exports()
        expected = original | ADDITIONS
        if actual != expected:
            problems.append(f"export difference from original+six: {sorted(actual ^ expected)}")
        missing_npp = npp_imports() - actual
        if missing_npp:
            problems.append(f"missing Notepad++ imports: {sorted(missing_npp)}")

        native = json.loads(NATIVE.read_text(encoding="utf-8"))["dlls"]
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
            module = descriptor.dll.decode("ascii").upper()
            if module not in {"KERNEL32.DLL", "USER32.DLL", "GDI32.DLL"}:
                problems.append(f"unexpected native dependency: {module}")
                continue
            missing_native = {
                entry.name.decode("ascii") if entry.name else f"#{entry.ordinal}"
                for entry in descriptor.imports
            } - set(native[module])
            if missing_native:
                problems.append(f"{module} imports absent from Win98 OEM: {sorted(missing_native)}")
    return problems


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/uxtheme-known/UXTHEME.DLL"
    try:
        problems = check(path)
    except (OSError, ValueError, AttributeError, AssertionError, pefile.PEFormatError) as exc:
        print(f"FAIL: UXTHEME KnownDLL PE inspection: {exc}", file=sys.stderr)
        return 1
    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    if problems:
        return 1
    print("PASS: KernelEx UXTHEME KnownDLL PE32 4.10, original exports plus six, pinned NPP imports and OEM-native dependencies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
