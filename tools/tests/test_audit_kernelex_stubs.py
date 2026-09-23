"""Small source fixtures for the KernelEx declaration/body distinction."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audit_kernelex_stubs as audit


class KernelExStubAuditTests(unittest.TestCase):
    def test_lexical_mask_preserves_positions_without_comment_false_hits(self) -> None:
        source = '/* DECL_API("Fake", bad) */\nchar *s = "// literal"; // hidden\nDECL_API("Real", good);\n'
        masked = audit.lexical_mask(source, keep_strings=True)
        self.assertEqual(len(masked), len(source))
        self.assertEqual(masked.count("\n"), source.count("\n"))
        self.assertNotIn("Fake", masked)
        self.assertNotIn("hidden", masked)
        self.assertIn('"// literal"', masked)
        self.assertIn('DECL_API("Real", good)', masked)

    def test_whole_table_classification_and_app_subset(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            base = root / "apilibs" / "kexbases" / "Kernel32"
            base.mkdir(parents=True)
            (base / "_kernel32_apilist.c").write_text(
                '/* DECL_API("Ghost", Ghost_new) */\n'
                'DECL_API("A", A_stub),\n'
                'DECL_API("B", B_new),\n'
                'DECL_API("C", C_fwd),\n'
                'DECL_API("D", D_new),\n'
                'DECL_API("E", GetSystemInfo),\n'
                'DECL_API("F", missing),\n'
                'DECL_API("G", G_stub),\n'
                'DECL_TAB("KERNEL32.DLL", names, 0);\n', encoding="utf-8")
            (base / "body.c").write_text(
                'UNIMPL_FUNC(A, 1);\n'
                'FORWARD_TO_UNICOWS(C);\n'
                'int B_new(int n) { FIXME("stub!"); return 1; }\n'
                'int D_new(int n) { return n + 1; }\n'
                'int G_stub(int n) { return 0xCAFE; }\n', encoding="utf-8")
            normal = root / "apilibs" / "kexbasen" / "winspool"
            normal.mkdir(parents=True)
            (normal / "_winspool_apilist.c").write_text(
                'DECL_API("Printer", Printer_new),\n'
                'DECL_TAB("WINSPOOL.DRV", names, 0);\n', encoding="utf-8")
            (normal / "body.c").write_text(
                'int Printer_new(int n) { return n; }\n', encoding="utf-8")
            native = root / "native.json"
            native.write_text(json.dumps({"dlls": {"KERNEL32.DLL": ["GetSystemInfo"]}}),
                              encoding="utf-8")
            app = root / "app.exe"
            app.write_bytes(b"synthetic test placeholder")
            with (mock.patch.object(audit.subprocess, "check_output",
                                    side_effect=["fake-commit\n", ""]),
                  mock.patch.object(audit, "pe_imports", return_value=[
                      {"dll": "KERNEL32.DLL", "symbol": "A", "kind": "load"},
                      {"dll": "KERNEL32.DLL", "symbol": "B", "kind": "load"},
                      {"dll": "KERNEL32.DLL", "symbol": "NoMatch", "kind": "load"},
                  ])):
                report = audit.build(root, native, app, root)
            by_name = {(x["dll"], x["symbol"]): x for x in report["entries"]}
            self.assertEqual(report["summary"]["declaration_rows"], 8)
            self.assertNotIn(("KERNEL32.DLL", "Ghost"), by_name)
            self.assertEqual(by_name["KERNEL32.DLL", "A"]["source_category"],
                             "explicit_unimplemented_macro")
            self.assertEqual(by_name["KERNEL32.DLL", "B"]["source_category"],
                             "suspicious_stub_body")
            self.assertEqual(by_name["KERNEL32.DLL", "C"]["source_category"],
                             "forward_to_unicows")
            self.assertEqual(by_name["KERNEL32.DLL", "D"]["source_category"],
                             "source_body_candidate")
            self.assertEqual(by_name["KERNEL32.DLL", "E"]["source_category"],
                             "native_alias_candidate")
            self.assertEqual(by_name["KERNEL32.DLL", "F"]["source_category"],
                             "unresolved_target")
            self.assertEqual(by_name["KERNEL32.DLL", "G"]["source_category"],
                             "explicit_stub_body")
            self.assertIn(("WINSPOOL.DRV", "Printer"), by_name)
            self.assertEqual(report["app_import_subset"]["kernelex_only_unique_names"], 2)
            self.assertEqual(report["app_import_subset"]["app_pe"], "app.exe")

    def test_actual_pinned_source_exposes_deceptive_success_stub(self) -> None:
        root = Path(__file__).resolve().parents[2] / "third_party" / "KernelEx"
        path = root / "apilibs" / "kexbases" / "Advapi32" / "security.c"
        if not path.exists():
            self.skipTest("KernelEx submodule was not checked out")
        source = audit.SourceFile.read(root, path)
        body = source.functions("CheckTokenMembership_new")
        self.assertEqual(len(body), 1)
        self.assertTrue(body[0]["stub_marker"])
        self.assertIn("*IsMember = TRUE;", source.raw)


if __name__ == "__main__":
    unittest.main()
