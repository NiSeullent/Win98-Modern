"""Original Win98 OEM import and loader gate for independent FLS fixture."""
import json
from pathlib import Path

import pefile


def main() -> int:
    native = set(json.loads(Path("benchmarks/win98se-ko-oem-native-exports-v1.json")
                            .read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
    failures = []
    for filename in ("FLSFIX.DLL", "fls_smoke.exe"):
        path = Path("build/fls") / filename
        pe = pefile.PE(str(path))
        header = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B:
            failures.append(f"{filename}: not PE32 i386")
        if (header.MajorSubsystemVersion, header.MinorSubsystemVersion) != (4, 10):
            failures.append(f"{filename}: not Win98 subsystem 4.10")
        if header.Subsystem != (2 if filename.endswith(".DLL") else 3):
            failures.append(f"{filename}: wrong subsystem")
        if header.DllCharacteristics & (0x40 | 0x100):
            failures.append(f"{filename}: ASLR/NX flags")
        for index in (9, 13, 14):
            if header.DATA_DIRECTORY[index].VirtualAddress:
                failures.append(f"{filename}: TLS/delay/CLR directory {index}")
        for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
            if descriptor.dll.upper() != b"KERNEL32.DLL":
                failures.append(f"{filename}: unexpected import DLL {descriptor.dll!r}")
            for entry in descriptor.imports:
                if entry.name is None:
                    failures.append(f"{filename}: ordinal import")
                elif entry.name.decode("ascii") not in native:
                    failures.append(f"{filename}: non-OEM import {entry.name!r}")
        pe.close()
    if failures:
        for message in failures:
            print("FAIL:", message)
        return 1
    print("PASS: FLS fixture/smoke PE32 Win98 OEM-only import gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
