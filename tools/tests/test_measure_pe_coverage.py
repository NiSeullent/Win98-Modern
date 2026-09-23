"""Small PE32 fixture tests for import coverage accounting."""
from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import measure_pe_coverage as coverage


def synthetic_pe(path: Path) -> None:
    image = bytearray(0xA00)

    def put(rva: int, value: bytes) -> None:
        offset = 0x200 + rva - 0x1000
        image[offset:offset + len(value)] = value

    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", image, 0x84,
                     0x14C, 1, 0, 0, 0, 0xE0, 0x2102)
    optional = 0x98
    struct.pack_into("<H", image, optional, 0x10B)
    struct.pack_into("<I", image, optional + 28, 0x400000)
    struct.pack_into("<I", image, optional + 92, 16)
    struct.pack_into("<II", image, optional + 96 + 0 * 8, 0x1300, 0xB0)
    struct.pack_into("<II", image, optional + 96 + 1 * 8, 0x1100, 40)
    struct.pack_into("<II", image, optional + 96 + 13 * 8, 0x1200, 64)
    section = optional + 0xE0
    image[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", image, section + 8, 0x800, 0x1000, 0x800, 0x200)

    put(0x1100, struct.pack("<IIIII", 0x1150, 0, 0, 0x1180, 0x1150))
    put(0x1150, struct.pack("<III", 0x1190, 0x80000005, 0))
    put(0x1180, b"KERNEL32.DLL\0")
    put(0x1190, b"\0\0CreateFileA\0")

    put(0x1200, struct.pack("<IIIIIIII", 1, 0x1250, 0,
                            0x1260, 0x1260, 0, 0, 0))
    put(0x1250, b"USER32.DLL\0")
    put(0x1260, struct.pack("<II", 0x1280, 0))
    put(0x1280, b"\0\0MessageBoxA\0")

    put(0x1300, struct.pack("<IIHHIIIIIII", 0, 0, 0, 0,
                            0x1360, 1, 2, 1, 0x1370, 0x1380, 0x1390))
    put(0x1360, b"SHIM.DLL\0")
    put(0x1370, struct.pack("<II", 0x1500, 0x1504))
    put(0x1380, struct.pack("<I", 0x13A0))
    put(0x1390, struct.pack("<H", 0))
    put(0x13A0, b"ShimEntry\0")
    path.write_bytes(image)


class PEImportCoverageTests(unittest.TestCase):
    def test_normal_delay_and_ordinal_imports(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "app.exe"
            synthetic_pe(path)
            self.assertEqual(
                coverage.pe_imports(path),
                [
                    {"dll": "KERNEL32.DLL", "symbol": "CreateFileA", "kind": "load"},
                    {"dll": "KERNEL32.DLL", "symbol": "#5", "kind": "load"},
                    {"dll": "USER32.DLL", "symbol": "MessageBoxA", "kind": "delay"},
                ],
            )
            self.assertEqual(
                coverage.pe_exports(path),
                {"SHIM.DLL": {"ShimEntry", "#1", "#2"}},
            )

    def test_report_separates_baseline_source_and_unresolved(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            target = folder / "app.exe"
            baseline = folder / "baseline.json"
            wrapper = folder / "wrapper.def"
            output = folder / "report.json"
            synthetic_pe(target)
            coverage.write_manifest(
                baseline, {"KERNEL32.DLL": {"CreateFileA", "#5"}},
                "synthetic test only",
            )
            wrapper.write_text("LIBRARY USER32.DLL\nEXPORTS\nMessageBoxA @10\n",
                               encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(coverage.main([
                    "report", "--baseline", str(baseline), "--app", f"Test={target}",
                    "--json-out", str(output),
                ]), 0)
            without = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(without["weighted_by_import_occurrence"]["matched"], 2)
            self.assertEqual(without["apps"][0]["unresolved"][0]["symbol"],
                             "MessageBoxA")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(coverage.main([
                    "report", "--baseline", str(baseline), "--app", f"Test={target}",
                    "--wrapper-source", str(wrapper), "--json-out", str(output),
                ]), 0)
            with_wrapper = json.loads(output.read_text(encoding="utf-8"))
            weighted = with_wrapper["weighted_by_import_occurrence"]
            self.assertEqual(weighted["counts"]["total"], 3)
            self.assertEqual(weighted["counts"]["baseline"], 2)
            self.assertEqual(weighted["counts"]["wrapper_source"], 1)
            self.assertEqual(weighted["matched_percent"], 100.0)
            self.assertEqual(weighted["baseline_or_artifact_percent"], 66.6667)

            addon = folder / "addon.dll"
            addon.write_bytes(target.read_bytes())
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(coverage.main([
                    "report", "--baseline", str(baseline),
                    "--app-file", f"Test={target}",
                    "--app-file", f"Test={addon}",
                    "--wrapper-source", str(wrapper), "--json-out", str(output),
                ]), 0)
            selected = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(len(selected["apps"][0]["files"]), 2)
            self.assertEqual(selected["weighted_by_import_occurrence"]["counts"]["total"], 6)

    def test_rejects_pe32_plus(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "wrong.exe"
            synthetic_pe(path)
            image = bytearray(path.read_bytes())
            struct.pack_into("<H", image, 0x98, 0x20B)
            path.write_bytes(image)
            with self.assertRaises(coverage.CoverageError):
                coverage.pe_imports(path)

    def test_app_peer_and_kernelex_source_are_separate_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            target = folder / "app.exe"
            peer = folder / "USER32.DLL"
            baseline = folder / "native.json"
            kernelex = folder / "source.json"
            output = folder / "report.json"
            synthetic_pe(target)
            synthetic_pe(peer)
            image = bytearray(peer.read_bytes())
            image[0x5A0:0x5AC] = b"MessageBoxA\0"
            peer.write_bytes(image)
            coverage.write_manifest(baseline, {"KERNEL32.DLL": {"CreateFileA"}},
                                    "synthetic native")
            coverage.write_manifest(kernelex, {"KERNEL32.DLL": {"#5"},
                                                       "USER32.DLL": {"MessageBoxA"}},
                                    "synthetic source")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(coverage.main([
                    "report", "--baseline", str(baseline),
                    "--kernelex-source-manifest", str(kernelex),
                    "--app-file", f"Test={target}",
                    "--app-peer-pe", f"Test={peer}",
                    "--json-out", str(output),
                ]), 0)
            report = json.loads(output.read_text(encoding="utf-8"))
            counts = report["apps"][0]["counts"]
            self.assertEqual(counts["baseline"], 1)
            self.assertEqual(counts["kernelex_source"], 1)
            self.assertEqual(counts["app_peer"], 1)
            self.assertEqual(report["apps"][0]["matched_percent"], 100.0)

    def test_zero_imports_are_not_100_percent(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "empty.exe"
            synthetic_pe(path)
            image = bytearray(path.read_bytes())
            struct.pack_into("<II", image, 0x98 + 96 + 1 * 8, 0, 0)
            struct.pack_into("<II", image, 0x98 + 96 + 13 * 8, 0, 0)
            path.write_bytes(image)
            result = coverage.measure_file(path, {}, {}, {})
            self.assertEqual(result["matched"], 0)
            self.assertIsNone(result["matched_percent"])

    def test_kernelex_named_and_ordinal_declarations(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "api.c"
            source.write_text(
                'DECL_API("SHCreateShellItem", shim),\n'
                'DECL_API(680, shim),\n'
                'DECL_TAB("SHELL32.DLL", names, ordinals);\n',
                encoding="utf-8",
            )
            self.assertEqual(
                coverage.source_exports([str(source)]),
                {"SHELL32.DLL": {"SHCreateShellItem", "#680"}},
            )

    def test_kernelex_driver_module_declarations(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "spool.c"
            source.write_text(
                'DECL_API("GetDefaultPrinterW", shim),\n'
                'DECL_API(7, shim),\n'
                'DECL_TAB("WINSPOOL.DRV", names, ordinals);\n',
                encoding="utf-8",
            )
            self.assertEqual(
                coverage.source_exports([str(source)]),
                {"WINSPOOL.DRV": {"GetDefaultPrinterW", "#7"}},
            )

    def test_project_named_and_ordinal_arrays_use_bound_target_dll(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "provider.c"
            source.write_text(
                'static const m98_named_api names[] = {\n'
                '  M98_NAMED("TaskDialogIndirect", impl),\n'
                '  { "LiteralEntry", (unsigned long)impl }\n};\n'
                'static const m98_ordinal_api numbers[] = {\n'
                '  M98_ORD(345, impl), M98_ORD(381, impl)\n};\n'
                'static const m98_named_api elsewhere[] = {\n'
                '  M98_API("UnboundEntry", impl)\n};\n'
                'static const m98_api_table tables[] = {\n'
                '  { "COMCTL32.DLL", names, 2, numbers, 2 },\n'
                '  { 0, 0, 0, 0, 0 }\n};\n', encoding="utf-8")
            self.assertEqual(
                coverage.source_exports([str(source)]),
                {"COMCTL32.DLL": {"TaskDialogIndirect", "LiteralEntry",
                                   "#345", "#381"}},
            )

    def test_project_array_type_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "provider.c"
            source.write_text(
                'static const m98_ordinal_api numbers[] = { M98_ORD(381, impl) };\n'
                'static const m98_api_table tables[] = {\n'
                '  { "COMCTL32.DLL", numbers, 1, 0, 0 }\n};\n',
                encoding="utf-8")
            with self.assertRaisesRegex(coverage.CoverageError,
                                        "wrong API array type"):
                coverage.source_exports([str(source)])


if __name__ == "__main__":
    unittest.main()
