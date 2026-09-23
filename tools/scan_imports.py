"""List PE imports and distinguish known KernelEx/M98 exports from unknowns.

Unknown means only that it was not found in either extension's API table.
Windows 98's native exports are deliberately not guessed by this scanner.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path


class PEError(ValueError):
    pass


class PEView:
    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        if len(self.data) < 0x40 or self.data[:2] != b"MZ":
            raise PEError("not an MZ executable")
        pe = self.u32(0x3C)
        if self.data[pe : pe + 4] != b"PE\0\0":
            raise PEError("not a PE executable")
        coff = pe + 4
        section_count = self.u16(coff + 2)
        optional_size = self.u16(coff + 16)
        optional = coff + 20
        magic = self.u16(optional)
        if magic == 0x10B:
            self.thunk_width = 4
            data_dir = optional + 96
        elif magic == 0x20B:
            self.thunk_width = 8
            data_dir = optional + 112
        else:
            raise PEError(f"unsupported optional header magic {magic:#x}")
        self.import_rva = self.u32(data_dir + 8)
        self.sections: list[tuple[int, int, int]] = []
        for i in range(section_count):
            offset = optional + optional_size + i * 40
            virtual_size = self.u32(offset + 8)
            virtual_address = self.u32(offset + 12)
            raw_size = self.u32(offset + 16)
            raw_address = self.u32(offset + 20)
            self.sections.append(
                (virtual_address, max(virtual_size, raw_size), raw_address)
            )

    def unpack(self, fmt: str, offset: int) -> tuple:
        if offset < 0 or offset + struct.calcsize(fmt) > len(self.data):
            raise PEError("PE structure points outside file")
        return struct.unpack_from(fmt, self.data, offset)

    def u16(self, offset: int) -> int:
        return self.unpack("<H", offset)[0]

    def u32(self, offset: int) -> int:
        return self.unpack("<I", offset)[0]

    def rva(self, address: int) -> int:
        for start, size, raw in self.sections:
            if start <= address < start + size:
                result = raw + address - start
                if result >= len(self.data):
                    break
                return result
        raise PEError(f"RVA {address:#x} is outside file sections")

    def string(self, address: int) -> str:
        start = self.rva(address)
        end = self.data.find(b"\0", start)
        if end < 0:
            raise PEError("unterminated PE string")
        return self.data[start:end].decode("ascii", errors="replace")

    def imports(self) -> list[tuple[str, str]]:
        if not self.import_rva:
            return []
        offset = self.rva(self.import_rva)
        result: list[tuple[str, str]] = []
        for _ in range(4096):
            original, stamp, chain, name_rva, first = self.unpack(
                "<IIIII", offset
            )
            if not (original or stamp or chain or name_rva or first):
                return result
            dll = self.string(name_rva).upper()
            thunk_rva = original or first
            thunk = self.rva(thunk_rva)
            for _ in range(65536):
                if self.thunk_width == 4:
                    value = self.u32(thunk)
                    ordinal_mask = 0x80000000
                else:
                    value = self.unpack("<Q", thunk)[0]
                    ordinal_mask = 0x8000000000000000
                if not value:
                    break
                if value & ordinal_mask:
                    symbol = f"#{value & 0xFFFF}"
                else:
                    symbol = self.string(value + 2)
                result.append((dll, symbol))
                thunk += self.thunk_width
            else:
                raise PEError("unterminated thunk table")
            offset += 20
        raise PEError("unterminated import directory")


def extension_exports(root: Path) -> set[tuple[str, str]]:
    source = (root / "src/m98wrap.c").read_text(encoding="utf-8")
    return {
        ("KERNEL32.DLL", symbol)
        for symbol in re.findall(r'M98_API\("([^\"]+)"', source)
    }


def kernelex_exports(root: Path) -> set[tuple[str, str]]:
    result: set[tuple[str, str]] = set()
    for path in (root / "third_party/KernelEx/apilibs").rglob("*_apilist.c"):
        source = path.read_text(encoding="latin-1")
        match = re.search(r'DECL_TAB\("([^\"]+)"', source)
        if not match:
            continue
        dll = match.group(1).upper()
        for symbol in re.findall(r'DECL_API\("([^\"]+)"', source):
            result.add((dll, symbol))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pe", type=Path, help="PE32 or PE32+ file to inspect")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    plugin = extension_exports(root)
    kernelex = kernelex_exports(root)
    rows = []
    for dll, symbol in PEView(args.pe).imports():
        key = (dll, symbol)
        if key in plugin:
            source = "m98wrap"
        elif key in kernelex:
            source = "KernelEx"
        else:
            source = "unknown_or_native"
        rows.append({"dll": dll, "symbol": symbol, "source": source})
    print(json.dumps(rows, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
