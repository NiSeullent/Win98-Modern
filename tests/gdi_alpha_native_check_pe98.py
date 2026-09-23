"""Static OEM-native import gate for the read-only GDI pixel probe."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"


def check(image_path: Path) -> list[str]:
    native = json.loads(MANIFEST.read_text(encoding="utf-8"))["dlls"]
    image = pefile.PE(str(image_path))
    try:
        errors: list[str] = []
        optional = image.OPTIONAL_HEADER
        if image.FILE_HEADER.Machine != 0x14C or optional.Magic != 0x10B:
            errors.append("expected PE32 i386")
        if image.is_dll() or optional.Subsystem != 3:
            errors.append("expected console executable")
        if (optional.MajorSubsystemVersion, optional.MinorSubsystemVersion) != (4, 10):
            errors.append("expected subsystem 4.10")
        if optional.DllCharacteristics & (0x0040 | 0x0100):
            errors.append("unexpected ASLR/NX flags")
        for index, name in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
            if optional.DATA_DIRECTORY[index].VirtualAddress:
                errors.append(f"unexpected {name}")
        for descriptor in image.DIRECTORY_ENTRY_IMPORT:
            library = descriptor.dll.decode("ascii").upper()
            if library not in ("GDI32.DLL", "KERNEL32.DLL"):
                errors.append(f"unexpected import library {library}")
                continue
            for entry in descriptor.imports:
                if entry.name is None:
                    errors.append(f"unexpected ordinal import from {library}")
                elif entry.name.decode("ascii") not in native[library]:
                    errors.append(f"absent OEM import {library}!{entry.name.decode('ascii')}")
        return errors
    finally:
        image.close()


if __name__ == "__main__":
    problems = check(Path(sys.argv[1]))
    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    if problems:
        raise SystemExit(1)
    print("PASS: PE32 console 4.10 with native Win98 GDI32/KERNEL32 imports")
