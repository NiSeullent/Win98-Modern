"""Check the guest agent against PE32 loader rules and the native ISO exports.

This static gate does not substitute for loading and exercising the agent in
the Windows 98 guest. The manifest comes from the project's OEM ISO baseline.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pefile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"


def check(image_path: Path) -> tuple[list[str], int]:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != "w98mod.export-manifest.v1":
        raise ValueError("native export manifest has an unexpected schema")
    native = set(manifest["dlls"]["KERNEL32.DLL"])
    issues: list[str] = []
    pe = pefile.PE(str(image_path), fast_load=False)
    try:
        header = pe.OPTIONAL_HEADER
        if pe.FILE_HEADER.Machine != 0x14C or header.Magic != 0x10B:
            issues.append("expected a PE32 x86 image")
        if pe.is_dll():
            issues.append("expected an executable, not a DLL")
        if header.Subsystem != 3 or (
            header.MajorSubsystemVersion,
            header.MinorSubsystemVersion,
        ) != (4, 10):
            issues.append("expected a console subsystem 4.10 image")
        if header.DllCharacteristics & (0x0040 | 0x0100):
            issues.append("ASLR/NX image flags are unsupported by Windows 98")
        for index, label in ((9, "TLS"), (13, "delay imports"), (14, "CLR")):
            if header.DATA_DIRECTORY[index].VirtualAddress:
                issues.append(f"unexpected {label} directory")

        imports = 0
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
            library = descriptor.dll.decode("ascii", errors="replace").upper()
            if library != "KERNEL32.DLL":
                issues.append(f"unsupported import library: {library}")
            for entry in descriptor.imports:
                imports += 1
                if entry.name is None:
                    issues.append(f"ordinal import from {library}: {entry.ordinal}")
                    continue
                name = entry.name.decode("ascii", errors="replace")
                if library == "KERNEL32.DLL" and name not in native:
                    issues.append(f"absent from native KERNEL32 manifest: {name}")
        if not imports:
            issues.append("no imports found; native API gate is inconclusive")
    finally:
        pe.close()
    return issues, imports


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else ROOT / "build/m98agent.exe"
    try:
        issues, imports = check(path)
    except (OSError, KeyError, ValueError, pefile.PEFormatError) as exc:
        print(f"FAIL: guest PE inspection: {exc}", file=sys.stderr)
        return 1
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print(f"PASS: PE32 console 4.10; {imports} native KERNEL32 imports in ISO manifest")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
