"""Verify x86/Win98 SList artifacts and exact CPU/native import dependencies."""
import json
from pathlib import Path

import pefile

API = {"InitializeSListHead", "InterlockedFlushSList", "InterlockedPopEntrySList",
       "InterlockedPushEntrySList", "InterlockedPushListSList",
       "InterlockedPushListSListEx", "QueryDepthSList"}
FILES = ("SLISTFIX.DLL", "slist_smoke.exe", "slist_import_probe.exe",
         "slist_integrated_probe.exe", "slist_fault_smoke.exe", "slist_native_probe.exe")


def main() -> int:
    native = set(json.loads(Path("benchmarks/win98se-ko-oem-native-exports-v1.json")
                           .read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
    failures = []
    for filename in FILES:
        pe = pefile.PE(str(Path("build/slist") / filename))
        opt = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14c or opt.Magic != 0x10b:
            failures.append(f"{filename}: not PE32 x86")
        if (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
            failures.append(f"{filename}: wrong subsystem version")
        if opt.Subsystem != (2 if filename.endswith(".DLL") else 3):
            failures.append(f"{filename}: wrong subsystem type")
        if opt.DllCharacteristics & (0x40 | 0x100 | 0x400):
            failures.append(f"{filename}: ASLR/NX/NO_SEH flags")
        for index in (9, 13, 14):
            if opt.DATA_DIRECTORY[index].VirtualAddress:
                failures.append(f"{filename}: unexpected TLS/delay/CLR directory")
        imports = set()
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            if descriptor.dll.upper() != b"KERNEL32.DLL":
                failures.append(f"{filename}: unexpected DLL {descriptor.dll!r}")
            for entry in descriptor.imports:
                if entry.name is None:
                    failures.append(f"{filename}: ordinal import")
                else:
                    imports.add(entry.name.decode("ascii"))
        expected = API if filename == "slist_import_probe.exe" else set()
        if filename == "slist_native_probe.exe":
            expected = {"InitializeSListHead", "InterlockedPushEntrySList",
                        "InterlockedPopEntrySList", "InterlockedFlushSList"}
        if imports & API != expected:
            failures.append(f"{filename}: family imports {sorted(imports & API)} != {sorted(expected)}")
        for name in sorted(imports - native - expected):
            failures.append(f"{filename}: import absent from original Win98: {name}")
        code = b"".join(section.get_data() for section in pe.sections
                        if section.Characteristics & 0x20000000)
        if filename in ("SLISTFIX.DLL", "slist_fault_smoke.exe") and b"\xf0\x0f\xc7" not in code:
            failures.append(f"{filename}: locked CMPXCHG8B instruction missing")
        if filename == "SLISTFIX.DLL":
            exports = {entry.name for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols}
            if exports != {b"get_api_table"}:
                failures.append(f"{filename}: fixture leaked internal/fault/test symbols")
            if imports:
                failures.append(f"{filename}: production SList module should have zero OS imports")
        pe.close()
    for message in failures:
        print("FAIL:", message)
    if failures:
        return 1
    print("PASS: SList PE32/Win98/SEH/CMPXCHG8B/native-import gate (fixture has zero OS imports)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
