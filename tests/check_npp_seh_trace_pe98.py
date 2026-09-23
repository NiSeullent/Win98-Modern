"""Static Windows 98 loader gate for the optional NPP debug-event tracer.

An OEM export-name match does not establish the Windows 98 debugger behavior.
That must be checked in the installed guest before this diagnostic is trusted.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"
REQUIRED = {
    "CreateProcessA", "WaitForDebugEvent", "ContinueDebugEvent",
    "ReadProcessMemory", "GetThreadContext", "GetCommandLineA",
    "GetStdHandle", "WriteFile", "GetTickCount", "TerminateProcess",
    "ExitProcess", "CloseHandle", "GetLastError",
}


def check(path: Path) -> list[str]:
    native = set(json.loads(NATIVE.read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
    image = pefile.PE(str(path), fast_load=False)
    issues: list[str] = []
    imports: set[str] = set()
    try:
        h = image.OPTIONAL_HEADER
        if image.FILE_HEADER.Machine != 0x14C or h.Magic != 0x10B or image.is_dll():
            issues.append("expected executable PE32 x86")
        if h.Subsystem != 3 or (h.MajorSubsystemVersion, h.MinorSubsystemVersion) != (4, 10):
            issues.append("expected console subsystem 4.10")
        if (h.MajorOperatingSystemVersion, h.MinorOperatingSystemVersion) != (4, 10):
            issues.append("expected minimum OS 4.10")
        if h.DllCharacteristics & (0x0040 | 0x0100):
            issues.append("ASLR/NX image flags unsupported by Windows 98")
        for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
            if h.DATA_DIRECTORY[index].VirtualAddress:
                issues.append(f"unexpected {label} directory")
        for descriptor in getattr(image, "DIRECTORY_ENTRY_IMPORT", ()):
            dll = descriptor.dll.decode("ascii", errors="replace").upper()
            if dll != "KERNEL32.DLL":
                issues.append(f"unexpected import module {dll}")
            for item in descriptor.imports:
                if item.name is None:
                    issues.append(f"ordinal import #{item.ordinal} from {dll}")
                    continue
                name = item.name.decode("ascii", errors="replace")
                imports.add(name)
                if name not in native:
                    issues.append(f"{dll}!{name} absent from Win98 OEM CAB manifest")
        for missing in sorted(REQUIRED - imports):
            issues.append(f"tracer API import absent: {missing}")
    finally:
        image.close()
    return issues


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else ROOT / "build/npp_seh_trace.exe"
    try:
        issues = check(path)
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: PE32 console 4.10, native Win98 KERNEL32 imports only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
