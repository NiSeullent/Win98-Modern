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
                                        "src/m98wrap.c", 20, "e" * 64)
        self.assertEqual(row["key"], "KERNEL32.DLL!FlsAlloc")
        self.assertEqual(row["kind"], "export_declaration")

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
