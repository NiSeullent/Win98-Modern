"""Check i386/Win98 PE headers and OEM-native imports for work-object fixtures."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "threadpool"
WORK_NAMES = {
    "CloseThreadpoolWork",
    "CreateThreadpoolWork",
    "FreeLibraryWhenCallbackReturns",
    "SubmitThreadpoolWork",
    "WaitForThreadpoolWorkCallbacks",
}
CALLBACK_NAMES = WORK_NAMES | {
    "CallbackMayRunLong", "DisassociateCurrentThreadFromCallback",
    "LeaveCriticalSectionWhenCallbackReturns", "ReleaseMutexWhenCallbackReturns",
    "ReleaseSemaphoreWhenCallbackReturns", "SetEventWhenCallbackReturns",
    "TrySubmitThreadpoolCallback",
}


def check(path: Path, *, dll: bool, native: set[str],
          static: bool = False, fixture: bool = False,
          callbacks: bool = False) -> list[str]:
    errors: list[str] = []
    pe = pefile.PE(str(path))
    opt = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or opt.Magic != 0x10B:
        errors.append(f"{path.name}: expected i386 PE32")
    if pe.is_dll() != dll:
        errors.append(f"{path.name}: wrong image kind")
    if (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
        errors.append(f"{path.name}: expected Win98 subsystem 4.10")
    if opt.Subsystem != (2 if dll else 3):
        errors.append(f"{path.name}: wrong subsystem")
    if opt.DllCharacteristics & (0x0040 | 0x0100):
        errors.append(f"{path.name}: ASLR/NX flags enabled")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if opt.DATA_DIRECTORY[index].VirtualAddress:
            errors.append(f"{path.name}: unsupported {label}")
    imports: dict[str, set[str]] = {}
    for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        library = descriptor.dll.decode("ascii").upper()
        names = imports.setdefault(library, set())
        for entry in descriptor.imports:
            if entry.name is None:
                errors.append(f"{path.name}: ordinal import from {library}")
            else:
                names.add(entry.name.decode("ascii"))
    if set(imports) - {"KERNEL32.DLL"}:
        errors.append(f"{path.name}: unexpected import DLLs {sorted(imports)}")
    names = imports.get("KERNEL32.DLL", set())
    required = CALLBACK_NAMES if callbacks else WORK_NAMES
    if static and names & required != required:
        errors.append(f"{path.name}: missing static work imports {sorted(required - names)}")
    if not static and names & CALLBACK_NAMES:
        errors.append(f"{path.name}: fixture has unresolved work imports")
    for name in sorted(names - native - (required if static else set())):
        errors.append(f"{path.name}: unverified Win98 OEM import {name}")
    if fixture:
        exports = {
            entry.name.decode("ascii")
            for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols
            if entry.name is not None
        }
        if "get_api_table" not in exports:
            errors.append(f"{path.name}: no KernelEx API table export")
        required = {"CreateThread", "CreateSemaphoreA", "ReleaseSemaphore",
                    "WaitForSingleObject", "CreateEventA", "SetEvent", "HeapAlloc",
                    "HeapFree", "FreeLibrary"}
        for name in sorted(required - names):
            errors.append(f"{path.name}: missing native worker primitive {name}")
    pe.close()
    return errors


def main() -> int:
    try:
        manifest = json.loads((ROOT / "benchmarks" /
                               "win98se-ko-oem-native-exports-v1.json")
                              .read_text(encoding="utf-8"))
        native = set(manifest["dlls"]["KERNEL32.DLL"])
        errors: list[str] = []
        errors += check(OUT / "threadpool_fixture.dll", dll=True,
                        native=native, fixture=True)
        errors += check(OUT / "TPMARK.DLL", dll=True, native=native)
        errors += check(OUT / "threadpool_smoke.exe", dll=False, native=native)
        errors += check(OUT / "threadpool_integrated_probe.exe", dll=False,
                        native=native)
        errors += check(OUT / "threadpool_import_probe.exe", dll=False,
                        native=native, static=True)
        errors += check(OUT / "threadpool_guest_import_smoke.exe", dll=False,
                        native=native, static=True)
        errors += check(OUT / "threadpool_relocation_probe.exe", dll=False,
                        native=native)
        errors += check(OUT / "threadpool_callback_direct.exe", dll=False, native=native)
        errors += check(OUT / "threadpool_callback_integrated.exe", dll=False, native=native)
        errors += check(OUT / "threadpool_callback_static.exe", dll=False, native=native,
                        static=True, callbacks=True)
        fixture = pefile.PE(str(OUT / "threadpool_fixture.dll"))
        marker = pefile.PE(str(OUT / "TPMARK.DLL"))
        if fixture.OPTIONAL_HEADER.ImageBase == marker.OPTIONAL_HEADER.ImageBase:
            errors.append("TPMARK.DLL: preferred base collides with fixture")
        reloc = marker.OPTIONAL_HEADER.DATA_DIRECTORY[5]
        if not reloc.VirtualAddress or not reloc.Size:
            errors.append("TPMARK.DLL: no base relocation directory")
        imports = {
            entry.name.decode("ascii")
            for descriptor in getattr(marker, "DIRECTORY_ENTRY_IMPORT", ())
            for entry in descriptor.imports if entry.name is not None
        }
        if "GetTickCount" not in imports:
            errors.append("TPMARK.DLL: missing native GetTickCount import")
        fixture.close()
        marker.close()
    except (OSError, ValueError, KeyError, pefile.PEFormatError) as exc:
        print(f"FAIL: threadpool PE98 gate: {exc}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: threadpool PE32/i386, Win98 native imports, work/callback static routes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
