"""Validate the lock-holder exit regression EXE against original Win98 exports."""

from __future__ import annotations

import json
from pathlib import Path
import sys

import pefile


ROOT = Path(__file__).resolve().parent.parent
REQUIRED = {
    "CreateProcessA", "CreateThread", "ExitProcess", "GetExitCodeProcess",
    "GetModuleFileNameA", "GetCommandLineA", "TerminateProcess",
    "WaitForSingleObject",
}


def check(path: Path) -> list[str]:
    errors: list[str] = []
    native = set(json.loads(
        (ROOT / "benchmarks" / "win98se-ko-oem-native-exports-v1.json")
        .read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
    pe = pefile.PE(str(path))
    opt = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B:
        errors.append("expected i386 PE32")
    if pe.is_dll() or opt.Subsystem != 3:
        errors.append("expected console EXE")
    if (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
        errors.append("expected Win98 subsystem 4.10")
    if opt.DllCharacteristics & (0x0040 | 0x0100):
        errors.append("ASLR/NX flags enabled")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if opt.DATA_DIRECTORY[index].VirtualAddress:
            errors.append(f"unsupported {label} directory")
    imports: dict[str, set[str]] = {}
    for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        library = descriptor.dll.decode("ascii").upper()
        names = imports.setdefault(library, set())
        for entry in descriptor.imports:
            if entry.name is None:
                errors.append(f"ordinal import from {library}")
            else:
                names.add(entry.name.decode("ascii"))
    if set(imports) != {"KERNEL32.DLL"}:
        errors.append(f"unexpected import DLLs: {sorted(imports)}")
    names = imports.get("KERNEL32.DLL", set())
    if names - native:
        errors.append(f"unverified OEM imports: {sorted(names - native)}")
    if REQUIRED - names:
        errors.append(f"missing regression imports: {sorted(REQUIRED - names)}")
    pe.close()
    return errors


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_threadpool_exit_pe98.py PATH", file=sys.stderr)
        return 2
    try:
        errors = check(Path(sys.argv[1]))
    except (OSError, ValueError, KeyError, pefile.PEFormatError) as exc:
        print(f"FAIL: threadpool exit PE98 gate: {exc}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"FAIL: threadpool exit PE98 gate: {error}", file=sys.stderr)
        return 1
    print("PASS: threadpool exit PE32/i386, Win98 4.10, OEM-native imports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
