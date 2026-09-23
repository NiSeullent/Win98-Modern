"""Focused declaration parsing and pinned-byte integrity tests."""

from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import index_api_sources as index  # noqa: E402


class SpecTests(unittest.TestCase):
    def parse(self, text: str):
        return index.parse_spec_line(text, "wine", "a" * 40,
                                     "dlls/kernel32/kernel32.spec", 7, "f" * 64)

    def test_flags_forward_and_ordinal(self):
        row, reason = self.parse("17 stdcall -i386 -version=0x600+ Foo(ptr) ntdll.RtlFoo")
        self.assertIsNone(reason)
        self.assertEqual(row["key"], "KERNEL32.DLL!Foo")
        self.assertEqual(row["kind"], "forward")
        self.assertEqual(row["ordinal"], 17)
        self.assertEqual(row["target"], "ntdll.RtlFoo")
        self.assertEqual(row["architecture_flags"], ["-i386"])
        self.assertEqual(row["condition_flags"], ["-i386", "-version=0x600+"])

    def test_ordinal_forward_target_survives_comment(self):
        row, reason = self.parse("@ stdcall Foo() OTHER.#123 # trailing comment")
        self.assertIsNone(reason)
        self.assertEqual(row["kind"], "forward")
        self.assertEqual(row["target"], "OTHER.#123")
        row, reason = self.parse("@ stdcall Foo() OTHER.#123; trailing comment")
        self.assertIsNone(reason)
        self.assertEqual(row["target"], "OTHER.#123")

    def test_stub_is_never_implemented(self):
        row, _ = self.parse("@ stdcall -stub Bar(ptr)")
        self.assertEqual(row["kind"], "stub")
        row, _ = self.parse("@ stub Baz")
        self.assertEqual(row["kind"], "stub")

    def test_data_and_internal_alias(self):
        row, _ = self.parse("@ extern GlobalCounter")
        self.assertEqual(row["kind"], "data")
        row, _ = self.parse("@ stdcall Foo() LocalFoo")
        self.assertEqual(row["kind"], "export_declaration")
        self.assertEqual(row["target"], "LocalFoo")

    def test_unparsed_api_set_and_bad_line_reported(self):
        self.assertEqual(self.parse("apiset api-ms-win-foo = kernelbase.dll")[1],
                         "apiset_mapping_not_export")
        self.assertEqual(self.parse("ACTIVE_NOT_SPEC")[1],
                         "unrecognized_spec_syntax")


class DefAndMacroTests(unittest.TestCase):
    def test_def_inline_exports_alias_ordinal_data(self):
        rows, unresolved = index.parse_def_text(
            "LIBRARY sample\nEXPORTS\tAlpha\n"
            "Beta=other.Beta @2 NONAME\nGamma @3 DATA\n",
            "reactos", "b" * 40, "dll/win32/sample/sample.def", "e" * 64)
        self.assertFalse(unresolved)
        self.assertEqual([r["name"] for r in rows], ["Alpha", "Beta", "Gamma"])
        self.assertEqual([r["kind"] for r in rows],
                         ["export_declaration", "forward", "data"])
        self.assertEqual(rows[1]["ordinal"], 2)
        self.assertEqual(rows[1]["dll"], "SAMPLE.DLL")

    def test_def_ordinal_forward_and_noname_survive_comment(self):
        rows, unresolved = index.parse_def_text(
            "LIBRARY example\nEXPORTS\nFoo=OTHER.#123 @42 NONAME ; note\n",
            "wine", "b" * 40, "dlls/example/example.def", "e" * 64)
        self.assertFalse(unresolved)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["target"], "OTHER.#123")
        self.assertEqual(rows[0]["kind"], "forward")
        self.assertEqual(rows[0]["ordinal"], 42)
        self.assertEqual(rows[0]["flags"], ["NONAME"])

    def test_kernel_ex_and_project_macros(self):
        row, _ = index.parse_macro_line('DECL_API("Foo", Foo_fwd),', "kernelex",
                                        "c" * 40,
                                        "third_party/KernelEx/apilibs/kexbases/Kernel32/_kernel32_apilist.c",
                                        10, "e" * 64)
        self.assertEqual((row["key"], row["kind"]),
                         ("KERNEL32.DLL!Foo", "forward"))
        row, _ = index.parse_macro_line('M98_API("FlsAlloc", m98_FlsAlloc),',
                                        "project", "working-tree@d",
                                        "src/m98wrap.c", 20, "e" * 64, "KERNEL32.DLL")
        self.assertEqual(row["key"], "KERNEL32.DLL!FlsAlloc")
        self.assertEqual(row["kind"], "export_declaration")

    def test_project_target_must_be_explicit(self):
        raw = 'M98_API("AddClipboardFormatListener", m98_Add),'
        row, reason = index.parse_macro_line(raw, "project", "working-tree@d",
                                            "src/m98user.c", 20, "e" * 64)
        self.assertIsNone(row)
        self.assertEqual(reason, "unresolved_project_table_target")
        row, reason = index.parse_macro_line(raw, "project", "working-tree@d",
                                            "src/m98user.c", 20, "e" * 64, "USER32.DLL")
        self.assertIsNone(reason)
        self.assertEqual(row["key"], "USER32.DLL!AddClipboardFormatListener")

    def test_project_named_and_ordinal_macro_identity(self):
        named, reason = index.parse_macro_line(
            'M98_NAMED("LoadIconWithScaleDown", m98_LoadIconWithScaleDown)',
            "project", "working-tree@d", "src/m98ctl.c", 9, "e" * 64,
            "COMCTL32.DLL")
        self.assertIsNone(reason)
        self.assertEqual((named["key"], named["ordinal"], named["declaration_type"]),
                         ("COMCTL32.DLL!LoadIconWithScaleDown", None, "M98_NAMED"))
        ordinal, reason = index.parse_macro_line(
            'M98_ORD(381, m98_LoadIconWithScaleDown)', "project",
            "working-tree@d", "src/m98ctl.c", 14, "e" * 64,
            "COMCTL32.DLL")
        self.assertIsNone(reason)
        self.assertEqual((ordinal["key"], ordinal["name"], ordinal["ordinal"],
                          ordinal["target"], ordinal["declaration_type"]),
                         ("COMCTL32.DLL!#381", "#381", 381,
                          "m98_LoadIconWithScaleDown", "M98_ORD"))
        self.assertEqual(index.parse_macro_line(
            'M98_ORD("381", bad)', "project", "x", "src/x.c", 1, "e" * 64,
            "COMCTL32.DLL")[1], "invalid_project_macro_argument")

    def test_macro_comments_and_conditions(self):
        text = "#if 0\n/* DECL_API(\"Bad\", Bad_new), */\nDECL_API(\"Good\", Good_new),\n#endif\n"
        lines = list(index.c_macro_lines(text))
        parsed = []
        for line, raw, conditions in lines:
            row, _ = index.parse_macro_line(raw, "kernelex", "c" * 40,
                                            "apilibs/kexbases/Kernel32/_kernel32_apilist.c",
                                            line, "e" * 64)
            if row:
                row["condition_flags"].extend(conditions)
                parsed.append(row)
        self.assertEqual([r["name"] for r in parsed], ["Good"])
        self.assertEqual(parsed[0]["condition_flags"], ["#if 0"])

    def test_rejected_macro_name_is_visible(self):
        row, reason = index.parse_macro_line(
            'DECL_API("NetGetDCName*/", NetGetDCName_new),', "kernelex",
            "c" * 40, "apilibs/kexbasen/netapi32/_netapi32_apilist.c",
            40, "e" * 64)
        self.assertIsNone(row)
        self.assertEqual(reason, "noncanonical_api_name")


class IntegrityTests(unittest.TestCase):
    @staticmethod
    def commit_repo(path: Path) -> str:
        subprocess.run(["git", "-C", str(path), "init", "-q"], check=True)
        subprocess.run(["git", "-C", str(path), "config", "core.autocrlf", "false"],
                       check=True)
        subprocess.run(["git", "-C", str(path), "add", "."], check=True)
        subprocess.run(["git", "-C", str(path),
                        "-c", "user.name=Catalog Test",
                        "-c", "user.email=catalog@example.invalid",
                        "commit", "-q", "-m", "pin"], check=True)
        return index.git_head(path)

    def test_project_tables_keep_dll_identity_and_ambiguous_rows(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "src/providers.c").write_text(
                'static const m98_named_api kernel[] = {\nM98_API("KernelOnly", k),\nM98_API("Shared", k2)\n};\n'
                'static const m98_named_api user[] = {\nM98_API("Clipboard", u),\nM98_API("Shared", u2)\n};\n'
                'static const m98_api_table tables[] = {\n'
                '  {"KERNEL32.DLL", kernel, 2, 0, 0},\n'
                '  {"USER32.DLL", user, 2, 0, 0}\n};\n'
                'M98_API("Unattached", bad)\n', encoding="utf-8")
            self.commit_repo(root)
            rows, unresolved, _ = index.project_macro_rows(root)
            self.assertEqual({r["key"] for r in rows},
                             {"KERNEL32.DLL!KernelOnly", "KERNEL32.DLL!Shared",
                              "USER32.DLL!Clipboard", "USER32.DLL!Shared"})
            self.assertEqual([r["reason"] for r in unresolved],
                             ["unresolved_project_table_target"])

    def test_project_comctl_provider_links_named_and_ordinal_arrays(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            source = (
                '#define M98_NAMED(name, impl) {name, impl}\n'
                'static const m98_named_api named[] = {\n'
                '  M98_NAMED("LoadIconWithScaleDown", m98_LoadIconWithScaleDown),\n'
                '  M98_NAMED("TaskDialogIndirect", m98_TaskDialogIndirect)\n};\n'
                'static const m98_ordinal_api ordinal[] = {\n'
                '  M98_ORD(345, m98_TaskDialogIndirect),\n'
                '  M98_ORD(381, m98_LoadIconWithScaleDown)\n};\n'
                'static const m98_api_table tables[] = {\n'
                '  {"COMCTL32.DLL", named, 2, ordinal, 2},\n'
                '  {0, 0, 0, 0, 0}\n};\n'
            )
            (root / "src/m98ctl.c").write_text(source, encoding="utf-8")
            self.commit_repo(root)
            rows, unresolved, _ = index.project_macro_rows(root)
            self.assertFalse(unresolved)
            self.assertEqual([(r["key"], r["line"], r["ordinal"]) for r in rows], [
                ("COMCTL32.DLL!LoadIconWithScaleDown", 3, None),
                ("COMCTL32.DLL!TaskDialogIndirect", 4, None),
                ("COMCTL32.DLL!#345", 7, 345),
                ("COMCTL32.DLL!#381", 8, 381),
            ])
            self.assertTrue(all(r["source_path"] == "src/m98ctl.c" and
                                r["source_file_sha256"] == hashlib.sha256(
                                    (root / "src/m98ctl.c").read_bytes()).hexdigest()
                                for r in rows))

    def test_project_table_ambiguity_and_unattached_rows_are_reported(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "src/providers.c").write_text(
                'static const m98_named_api shared[] = {\n'
                ' M98_NAMED("Shared", m98_Shared)\n};\n'
                'static const m98_ordinal_api unbound[] = {\n'
                ' M98_ORD(345, m98_Unbound)\n};\n'
                'static const m98_api_table tables[] = {\n'
                ' {"KERNEL32.DLL", shared, 1, 0, 0},\n'
                ' {"USER32.DLL", shared, 1, 0, 0}\n};\n'
                'M98_NAMED("Outside", m98_Outside);\n', encoding="utf-8")
            self.commit_repo(root)
            rows, unresolved, _ = index.project_macro_rows(root)
            self.assertFalse(rows)
            self.assertEqual([(r["line"], r["reason"]) for r in unresolved], [
                (2, "ambiguous_project_table_target"),
                (5, "unresolved_project_table_target"),
                (11, "unresolved_project_table_target"),
            ])

    def test_project_literal_table_entries_and_def_exports_are_retained(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "src/provider.c").write_text(
                'static const m98_named_api entries[] = {\n'
                ' { "RegGetValueA", (unsigned long)m98_RegGetValueA },\n'
                ' { "RegGetValueW", (unsigned long)m98_RegGetValueW }\n};\n'
                'static const m98_api_table api_tables[] = {\n'
                ' { "ADVAPI32.DLL", entries, 2, 0, 0 }\n};\n',
                encoding="utf-8")
            (root / "src/shim.def").write_text(
                'LIBRARY DWMAPI.DLL\nEXPORTS\n'
                ' DwmSetWindowAttribute=m98_DwmSetWindowAttribute\n',
                encoding="utf-8")
            self.commit_repo(root)
            rows, unresolved, _ = index.project_macro_rows(root)
            self.assertFalse(unresolved)
            self.assertEqual({r["key"] for r in rows}, {
                "ADVAPI32.DLL!RegGetValueA", "ADVAPI32.DLL!RegGetValueW",
                "DWMAPI.DLL!DwmSetWindowAttribute",
            })
            self.assertEqual([r["declaration_type"] for r in rows],
                             ["m98_named_api_literal", "m98_named_api_literal", "def"])
            self.assertEqual([(r["source_path"], r["line"]) for r in rows],
                             [("src/provider.c", 2), ("src/provider.c", 3),
                              ("src/shim.def", 3)])
            self.assertEqual(rows[-1]["source_file_sha256"], hashlib.sha256(
                (root / "src/shim.def").read_bytes()).hexdigest())

    def test_vxkex_is_export_metadata_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            local = root / "research" / "vxkex"
            module = local / "Extended" / "KxBase"
            module.mkdir(parents=True)
            (module / "kxbase.def").write_text(
                "LIBRARY KxBase\nEXPORTS\nOwnApi\nAlias=OTHER.#5 @7 NONAME\n",
                encoding="utf-8")
            (module / "forwards.c").write_text(
                '//#pragma comment(linker, "/EXPORT:Skipped=kernel32.Skipped")\n'
                '#pragma comment(linker, "/EXPORT:Real=kernel32.Real")\n',
                encoding="utf-8")
            revision = self.commit_repo(local)
            (module / "kxbase.def").write_text(
                "LIBRARY KxBase\nEXPORTS\nDirtyOnly\n", encoding="utf-8")
            (module / "forwards.c").write_text(
                '#pragma comment(linker, "/EXPORT:Dirty=kernel32.Dirty")\n',
                encoding="utf-8")
            (module / "untracked.def").write_text(
                "LIBRARY KxBase\nEXPORTS\nUntracked\n", encoding="utf-8")
            rows, unresolved = index.local_macro_rows(
                {"id": "vxkex", "revision": revision,
                 "local_root": "research/vxkex"}, root)
            self.assertFalse(unresolved)
            self.assertEqual([(x["name"], x["kind"]) for x in rows],
                             [("OwnApi", "export_declaration"),
                              ("Alias", "forward"), ("Real", "forward")])
            self.assertEqual(rows[1]["target"], "OTHER.#5")
            self.assertEqual(rows[1]["flags"], ["NONAME"])
            self.assertEqual(rows[2]["key"], "KXBASE.DLL!Real")
            for row in rows:
                self.assertEqual(row["reuse_policy"], "reference_metadata_only")
                self.assertNotIn("raw_declaration", row)
                self.assertEqual(len(row["source_file_sha256"]), 64)

    def test_kernelex_reads_pinned_blob_in_dirty_checkout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            local = root / "third_party" / "KernelEx"
            module = local / "apilibs" / "kexbases" / "Kernel32"
            module.mkdir(parents=True)
            pinned = b'DECL_API("Original", Original_new),\n'
            (module / "_kernel32_apilist.c").write_bytes(pinned)
            revision = self.commit_repo(local)
            (module / "_kernel32_apilist.c").write_bytes(
                b'DECL_API("DirtyOnly", Dirty_new),\n')
            (module / "extra_apilist.c").write_bytes(
                b'DECL_API("Untracked", Untracked_new),\n')
            rows, unresolved = index.local_macro_rows(
                {"id": "kernelex", "revision": revision,
                 "local_root": "third_party/KernelEx"}, root)
            self.assertFalse(unresolved)
            self.assertEqual([row["name"] for row in rows], ["Original"])
            self.assertEqual(rows[0]["source_file_sha256"],
                             hashlib.sha256(pinned).hexdigest())

    def test_kernelex_uses_pinned_decl_tab_identity_for_winspool_drv(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            local = root / "third_party" / "KernelEx"
            module = local / "apilibs" / "kexbasen" / "winspool"
            module.mkdir(parents=True)
            apilist = module / "_winspool_apilist.c"
            pinned = (
                b'static const apilib_named_api names[] = {\n'
                b'  DECL_API("AddJobW", AddJobW_fwd),\n'
                b'  DECL_API("GetDefaultPrinterW", GetDefaultPrinterW_new),\n'
                b'};\n'
                b'const apilib_api_table table = '
                b'DECL_TAB("WINSPOOL.DRV", names, 0);\n'
            )
            apilist.write_bytes(pinned)
            revision = self.commit_repo(local)
            # The working copy must not be allowed to change the pinned identity.
            apilist.write_bytes(pinned.replace(b"WINSPOOL.DRV", b"WINSPOOL.DLL"))
            rows, unresolved = index.local_macro_rows(
                {"id": "kernelex", "revision": revision,
                 "local_root": "third_party/KernelEx"}, root)
            self.assertFalse(unresolved)
            self.assertEqual([row["key"] for row in rows],
                             ["WINSPOOL.DRV!AddJobW",
                              "WINSPOOL.DRV!GetDefaultPrinterW"])
            self.assertEqual([row["kind"] for row in rows],
                             ["forward", "export_declaration"])
            self.assertTrue(all(row["source_file_sha256"] ==
                                hashlib.sha256(pinned).hexdigest() for row in rows))

    def test_kernelex_conflicting_decl_tab_names_stay_unresolved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            local = root / "third_party" / "KernelEx"
            module = local / "apilibs" / "kexbasen" / "winspool"
            module.mkdir(parents=True)
            (module / "_winspool_apilist.c").write_text(
                'DECL_API("GetDefaultPrinterW", GetDefaultPrinterW_new),\n'
                'DECL_TAB("WINSPOOL.DRV", names, 0);\n'
                'DECL_TAB("WINSPOOL.DLL", other, 0);\n', encoding="utf-8")
            revision = self.commit_repo(local)
            rows, unresolved = index.local_macro_rows(
                {"id": "kernelex", "revision": revision,
                 "local_root": "third_party/KernelEx"}, root)
            self.assertFalse(rows)
            self.assertEqual([item["reason"] for item in unresolved],
                             ["ambiguous_kernelex_table_dll"])

    def test_archive_hash_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / "build/api-sources"
            folder.mkdir(parents=True)
            revision = "a" * 40
            archive = folder / f"wine-{revision}.tar.gz"
            archive.write_bytes(b"fake archive")
            source = {"id": "wine", "revision": revision,
                      "archive_url": "https://example.invalid/archive"}
            receipt = {"id": "wine", "revision": revision,
                       "url": source["archive_url"],
                       "path": archive.relative_to(root).as_posix(),
                       "bytes": len(b"fake archive"), "sha256": "0" * 64}
            with self.assertRaisesRegex(ValueError, "SHA/size mismatch"):
                index.verified_archive(source, receipt, root)
            receipt["sha256"] = hashlib.sha256(b"fake archive").hexdigest()
            self.assertEqual(index.verified_archive(source, receipt, root), archive)

    def test_archive_prefix_file_hash_and_exclusions(self):
        with tempfile.TemporaryDirectory() as temporary:
            revision = "b" * 40
            archive = Path(temporary) / "wine.tar.gz"
            data = b"@ stdcall -arch=i386 Foo(ptr)\n@ stub Bad\n"
            with tarfile.open(archive, "w:gz") as package:
                for name, payload in (
                    (f"wine-{revision}/dlls/foo/foo.spec", data),
                    (f"wine-{revision}/dlls/foo/tests/test.spec", b"@ stub Test\n"),
                ):
                    info = tarfile.TarInfo(name)
                    info.size = len(payload)
                    package.addfile(info, io.BytesIO(payload))
            source = {"id": "wine", "revision": revision}
            rows, unresolved, excluded, prefix = index.archive_rows(source, archive)
            self.assertEqual(prefix, f"wine-{revision}")
            self.assertEqual([r["kind"] for r in rows],
                             ["export_declaration", "stub"])
            self.assertEqual(rows[0]["source_file_sha256"],
                             hashlib.sha256(data).hexdigest())
            self.assertEqual(rows[0]["condition_flags"], ["-arch=i386"])
            self.assertFalse(unresolved)
            self.assertEqual(excluded[0]["reason"], "nonruntime_path:tests")


if __name__ == "__main__":
    unittest.main()
