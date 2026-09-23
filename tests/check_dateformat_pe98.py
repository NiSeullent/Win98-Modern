"""Static PE gate for the GetDateFormatEx DLL and guest import probe.

The wrapper must load on the original Win98 OEM KERNEL32. This gate does not
prove that KernelEx routes the new import or that the guest NLS data matches NT.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile

from check_pe98 import check as check_wrapper


def imports(pe: pefile.PE) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
        library = descriptor.dll.decode("ascii").upper()
        result[library] = {
            entry.name.decode("ascii") for entry in descriptor.imports
            if entry.name is not None
        }
    return result


def check(dll_path: Path, probe_path: Path, manifest_path: Path) -> list[str]:
    issues = check_wrapper(dll_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    native = set(manifest["dlls"]["KERNEL32.DLL"])

    dll = pefile.PE(str(dll_path))
    for library, names in imports(dll).items():
        if library != "KERNEL32.DLL":
            issues.append(f"date wrapper imports {library}")
        for name in sorted(names - native):
            issues.append(f"{name} absent from pinned Win98 OEM KERNEL32")
    dll.close()

    probe = pefile.PE(str(probe_path))
    optional = probe.OPTIONAL_HEADER
    if probe.FILE_HEADER.Machine != 0x14C or optional.Magic != 0x10B:
        issues.append("import probe is not PE32 i386")
    if optional.Subsystem != 3 or (
        optional.MajorSubsystemVersion,
        optional.MinorSubsystemVersion,
    ) != (4, 10):
        issues.append("import probe is not console subsystem 4.10")
    if optional.DllCharacteristics & (0x0040 | 0x0100):
        issues.append("import probe carries ASLR/NX flags")
    for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
        if optional.DATA_DIRECTORY[index].VirtualAddress:
            issues.append(f"import probe has unsupported {label}")
    probe_imports = imports(probe)
    if set(probe_imports) != {"KERNEL32.DLL"}:
        issues.append(f"import probe DLL set: {set(probe_imports)}")
    elif "GetDateFormatEx" not in probe_imports["KERNEL32.DLL"]:
        issues.append("GetDateFormatEx is not a static KERNEL32 import")
    else:
        for name in sorted(probe_imports["KERNEL32.DLL"] - native - {"GetDateFormatEx"}):
            issues.append(f"probe imports unverified Win98 OEM symbol {name}")
    probe.close()
    return issues


def main() -> int:
    dll_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/dateformat/m98wrap.dll")
    probe_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("build/dateformat/dateformat_import_probe.exe")
    manifest_path = Path("benchmarks/win98se-ko-oem-native-exports-v1.json")
    try:
        issues = check(dll_path, probe_path, manifest_path)
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: date wrapper static gate: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print("PASS: GetDateFormatEx Win98 PE/import static gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
