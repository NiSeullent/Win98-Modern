"""Win98 PE/import gate for InitOnce fixture, direct and static tests."""
import json
from pathlib import Path

import pefile

API = {"InitOnceBeginInitialize", "InitOnceComplete", "InitOnceExecuteOnce", "InitOnceInitialize"}


def main() -> int:
    native = set(json.loads(Path("benchmarks/win98se-ko-oem-native-exports-v1.json")
                            .read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
    failures = []
    for filename in ("ONCEFIX.DLL", "initonce_smoke.exe", "initonce_import_probe.exe",
                     "initonce_integrated_probe.exe"):
        path = Path("build/initonce") / filename
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
        actual = set()
        for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
            if descriptor.dll.upper() != b"KERNEL32.DLL":
                failures.append(f"{filename}: unexpected import DLL {descriptor.dll!r}")
            for entry in descriptor.imports:
                if entry.name is None:
                    failures.append(f"{filename}: ordinal import")
                else:
                    actual.add(entry.name.decode("ascii"))
        expected = API if filename == "initonce_import_probe.exe" else set()
        if actual & API != expected:
            failures.append(f"{filename}: InitOnce imports {sorted(actual & API)} != {sorted(expected)}")
        for name in sorted(actual - native - expected):
            failures.append(f"{filename}: import not in original Win98 manifest: {name}")
        pe.close()
    if failures:
        for message in failures:
            print("FAIL:", message)
        return 1
    print("PASS: InitOnce fixture and probes PE32/Win98/native-import gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
