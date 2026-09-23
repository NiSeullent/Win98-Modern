"""Validate whole FLS/lifecycle static binding versus direct-table probes."""
import json
from pathlib import Path
import pefile

ROOT = Path(__file__).resolve().parents[1]
APIS = {"FlsAlloc", "FlsFree", "FlsGetValue", "FlsSetValue", "CreateThread",
        "ExitThread", "FreeLibraryAndExitThread", "CreateFiber", "CreateFiberEx",
        "ConvertThreadToFiber", "ConvertThreadToFiberEx", "SwitchToFiber", "DeleteFiber"}
NEW_APIS = {"FlsAlloc", "FlsFree", "FlsGetValue", "FlsSetValue",
            "CreateFiberEx", "ConvertThreadToFiberEx"}

def main():
    native = set(json.loads((ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json")
                           .read_text())["dlls"]["KERNEL32.DLL"])
    failures = []
    for mode in ("static", "direct", "integrated"):
        path = ROOT / f"build/fls-integration/fls_{mode}.exe"
        pe = pefile.PE(str(path))
        opt = pe.OPTIONAL_HEADER
        def require(condition, message):
            if not condition:
                failures.append(f"{path.name}: {message}")
        require(pe.FILE_HEADER.Machine == 0x14c and opt.Magic == 0x10b
                and not pe.is_dll(), "expected PE32 i386 executable")
        require((opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) == (4, 10),
                "expected subsystem version 4.10")
        require(opt.Subsystem == 3 and not opt.DllCharacteristics & (0x40 | 0x100),
                "expected console image without ASLR/NX")
        require(not any(opt.DATA_DIRECTORY[i].VirtualAddress for i in (9, 13, 14)),
                "unexpected TLS/delay import/CLR directory")
        names = set()
        for desc in pe.DIRECTORY_ENTRY_IMPORT:
            require(desc.dll.upper() == b"KERNEL32.DLL", f"unexpected import DLL: {desc.dll!r}")
            for entry in desc.imports:
                require(entry.name is not None, "unexpected ordinal import")
                if entry.name is not None:
                    names.add(entry.name.decode("ascii"))
        if mode == "static":
            require(APIS <= names, f"missing family imports: {sorted(APIS - names)}")
            require(not names - native - NEW_APIS,
                    f"non-OEM imports outside family: {sorted(names - native - NEW_APIS)}")
        else:
            require(not names & APIS, f"direct probe imports tested APIs: {sorted(names & APIS)}")
            require(not names - native, f"non-OEM imports: {sorted(names - native)}")
        pe.close()
    if failures:
        for message in failures:
            print("FAIL:", message)
        return 1
    print("PASS: FLS whole-family static imports and native-only table probes PE98 gate")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
