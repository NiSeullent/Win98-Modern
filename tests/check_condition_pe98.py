"""PE32/Win98 import gate for real SRW condition-variable probes."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile

from check_pe98 import check as check_wrapper


CONDITION = {
    "InitializeConditionVariable",
    "SleepConditionVariableSRW",
    "WakeAllConditionVariable",
    "WakeConditionVariable",
}
SRW = {
    "InitializeSRWLock",
    "AcquireSRWLockExclusive",
    "ReleaseSRWLockExclusive",
}


def check_exe(path: Path, native: set[str], static: bool) -> list[str]:
    issues: list[str] = []
    pe = pefile.PE(str(path))
    header = pe.OPTIONAL_HEADER
    if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B:
        issues.append(f"{path.name}: not PE32 i386")
    if header.Subsystem != 3 or (
        header.MajorSubsystemVersion,
        header.MinorSubsystemVersion,
    ) != (4, 10):
        issues.append(f"{path.name}: not console subsystem 4.10")
    if header.DllCharacteristics & (0x0040 | 0x0100):
        issues.append(f"{path.name}: unsupported ASLR/NX flags")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if header.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"{path.name}: unsupported {label}")
    imports: dict[str, set[str]] = {}
    for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
        library = descriptor.dll.decode("ascii").upper()
        names = imports.setdefault(library, set())
        for entry in descriptor.imports:
            if entry.name is None:
                issues.append(f"{path.name}: ordinal import from {library}")
            else:
                names.add(entry.name.decode("ascii"))
    if set(imports) != {"KERNEL32.DLL"}:
        issues.append(f"{path.name}: DLL set {set(imports)}")
    else:
        names = imports["KERNEL32.DLL"]
        expected = CONDITION | SRW if static else set()
        actual = names & (CONDITION | SRW)
        if actual != expected:
            issues.append(f"{path.name}: new imports {sorted(actual)}, expected {sorted(expected)}")
        for name in sorted(names - native - expected):
            issues.append(f"{path.name}: unverified Win98 import {name}")
    pe.close()
    return issues


def main() -> int:
    base = Path("build/condition")
    try:
        native = set(json.loads(Path(
            "benchmarks/win98se-ko-oem-native-exports-v1.json"
        ).read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
        issues = check_wrapper(base / "m98wrap.dll")
        issues.extend(check_exe(base / "condition_smoke.exe", native, False))
        issues.extend(check_exe(base / "condition_import_probe.exe", native, True))
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: condition-variable PE gate: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: SRW condition-variable Win98 PE/import static gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
