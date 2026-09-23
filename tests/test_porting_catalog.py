"""Prevent catalogue loss, mistaken identity joins and false coverage claims."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_porting_catalog as catalog


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.groups = catalog.read_json(ROOT / "porting/groups.json")

    def upstream(self, name="InitOnceComplete", kind="stub"):
        return {"source":"wine", "revision":"a"*40, "dll":"kernel32.dll", "name":name,
                "kind":kind, "target":None, "ordinal":1, "source_path":"dlls/kernel32/kernel32.spec",
                "line":1, "source_file_sha256":"b"*64}

    def test_all_sdk_candidates_preserved_without_com_name_join(self):
        sdk = [
            {"id":"win32:KERNEL32.DLL!InitOnceComplete", "category":"win32_named_export_candidate", "dll":"KERNEL32.DLL", "name":"InitOnceComplete"},
            {"id":"com:{one}::InitOnceComplete#1", "category":"com_idl_method_candidate", "iid":"{one}", "interface":"ITest", "method":"InitOnceComplete", "dll":"KERNEL32.DLL", "name":"InitOnceComplete"},
            {"id":"winrt:Example.InitOnceComplete#1", "category":"winrt_metadata_method_candidate", "namespace":"Example", "method":"InitOnceComplete"},
            {"id":"winrt:Example.InitOnceComplete#2", "category":"winrt_metadata_method_candidate", "namespace":"Example", "method":"InitOnceComplete"},
        ]
        rows = catalog.merge_records(sdk, [self.upstream()], {}, {}, self.groups)
        self.assertEqual({r["id"] for r in rows}, {r["id"] for r in sdk})
        for row in rows:
            self.assertEqual(bool(row["upstream_declarations"]), row["id"].startswith("win32:"))
            self.assertFalse(row["full_compatibility_verified"])
            self.assertEqual(row["behavioral_coverage"], "unassessed")
        queue = catalog.build_queue(rows, self.groups)
        members = [v for b in queue["batches"] for v in b["members"]]
        self.assertEqual(sorted(members), sorted(r["id"] for r in sdk))

    def test_stub_forward_data_and_ordinal_are_retained_not_completed(self):
        declarations = [self.upstream("Stub", "stub"), self.upstream("Forward", "forward"),
                        self.upstream("Data", "data"), self.upstream("#7", "export_declaration")]
        rows = catalog.merge_records([], declarations, {"KERNEL32.DLL":{"NativeOnly"}},
            {("KERNEL32.DLL","ProjectOnly"):[{"kind":"project_export_declaration"}]}, self.groups)
        self.assertEqual(len(rows), 6)
        self.assertEqual({r["name"] for r in rows}, {"Stub","Forward","Data","#7","NativeOnly","ProjectOnly"})
        self.assertTrue(all(not r["full_compatibility_verified"] for r in rows))
        self.assertEqual(next(r for r in rows if r["name"]=="ProjectOnly")["implementation_status"], "declared_in_project")
        self.assertEqual(next(r for r in rows if r["name"]=="Stub")["implementation_status"], "listed")

    def test_aliases_remain_separate(self):
        sdk = [{"id":"contract", "category":"win32_api_set_alias", "dll":"api-ms-test.dll", "name":"InitOnceComplete"}]
        rows = catalog.merge_records(sdk, [self.upstream()], {}, {}, self.groups)
        self.assertEqual(len(rows), 2)
        contract = next(r for r in rows if r["id"]=="contract")
        self.assertEqual(contract["batch"], "api-set-routing")
        self.assertFalse(contract["upstream_declarations"])

    def test_family_queue_never_promotes_subset_to_complete(self):
        rows = catalog.merge_records([], [self.upstream()], {}, {}, self.groups)
        for coverage, expected, count in (
            ("unassessed", "awaiting_contract_review", 0),
            ("guest_static_subset_verified", "partial_guest_subset_evidence", 1),
            ("historical_guest_subset_evidence", "historical_evidence_requires_revalidation", 0),
        ):
            rows[0]["behavioral_coverage"] = coverage
            batch = catalog.build_queue(rows, self.groups)["batches"][0]
            self.assertEqual(batch["state"], expected)
            self.assertEqual(batch["guest_subset_verified_candidates"], count)
            self.assertIn("application_regression", batch["required_gates"])

    def test_unrelated_callbacks_not_assigned_to_threadpool(self):
        for name in ("NtCallbackReturn", "ZwCallbackReturn", "KiUserCallbackDispatcher", "RtlInstallFunctionTableCallback", "RegisterApplicationRecoveryCallback"):
            row = {"category":"supplemental_export_declaration", "dll":"NTDLL.DLL", "name":name}
            self.assertNotEqual(catalog.assign_batch(row, self.groups)["id"], "threadpool")
        row = {"category":"winrt_idl_method_candidate", "iid":"{sample}", "interface":"IExample", "method":"Close"}
        self.assertEqual(catalog.assign_batch(row, self.groups)["id"], "winrt-iid:{sample}")

    def test_complete_clipboard_family_including_native_and_supplemental(self):
        for dll,name in (("USER32.DLL","AddClipboardFormatListener"),
                         ("USER32.DLL","GetClipboardData"),
                         ("USER32.DLL","GetClipboardMetadata"),
                         ("WIN32U.DLL","NtUserGetClipboardData")):
            row={"category":"supplemental_export_declaration","dll":dll,"name":name}
            self.assertEqual(catalog.assign_batch(row,self.groups)["id"],"clipboard")
        row={"category":"win32_api_set_alias","dll":"api-ms-user.dll","name":"OpenClipboard"}
        self.assertEqual(catalog.assign_batch(row,self.groups)["id"],"api-set-routing")

    def test_project_literal_and_macro_tables(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/literal.c").write_text('static x apis[] = { { "RegGetValueA", (unsigned long)implementation } };\nstatic y tables[] = { { "ADVAPI32.DLL", apis, 1 } };\n')
            (root / "src/macro.c").write_text('static x apis[] = { M98_API("InitOnceComplete", impl) };\nstatic y tables[] = { { "KERNEL32.DLL", apis, 1 } };\n')
            (root / "src/dwm.def").write_text('LIBRARY DWMAPI.DLL\nEXPORTS\n DwmFlush=impl\n')
            parsed, hashes = catalog.read_project(root)
            self.assertEqual(set(parsed), {("ADVAPI32.DLL","RegGetValueA"), ("KERNEL32.DLL","InitOnceComplete"), ("DWMAPI.DLL","DwmFlush")})
            self.assertEqual(len(hashes), 3)
            self.assertTrue(all(v[0]["lines"] for v in parsed.values()))

    def test_dependency_cycles_and_missing_ids_rejected(self):
        catalog.validate_groups(self.groups)
        for dependency in ("abi-loader", "nonexistent"):
            broken = json.loads(json.dumps(self.groups))
            broken["foundations"][0]["depends_on"] = [dependency]
            with self.assertRaises(ValueError):
                catalog.validate_groups(broken)

    def test_duplicate_sdk_identity_rejected(self):
        item = {"id":"one", "category":"win32_named_export_candidate", "dll":"one.dll", "name":"Fn"}
        with self.assertRaises(ValueError):
            catalog.merge_records([item,item], [], {}, {}, self.groups)

    def test_csv_formula_escape_and_reproducible_gzip(self):
        self.assertEqual(catalog.excel_safe("@mangled"), "'@mangled")
        rows = catalog.merge_records([], [self.upstream()], {}, {}, self.groups)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            catalog.write_catalog(rows, out)
            first = (out / "catalog.jsonl.gz").read_bytes()
            catalog.write_catalog(rows, out)
            self.assertEqual(first, (out / "catalog.jsonl.gz").read_bytes())

    def test_receipt_and_verified_sources_cross_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build").mkdir()
            archive = root / "build/source.tar.gz"
            archive.write_bytes(b"test archive")
            manifest = {"sources":[{"id":"wine","revision":"a"*40,"archive_url":"https://example.invalid/source"}]}
            manifest_path = root / "sources.json"
            manifest_path.write_text(json.dumps(manifest))
            receipt = {"schema":"w98mod.api-source-receipt.v1", "manifest_sha256":catalog.digest(manifest_path),
                "archives":[{"id":"wine","revision":"a"*40,"url":"https://example.invalid/source",
                    "path":"build/source.tar.gz","bytes":archive.stat().st_size,"sha256":catalog.digest(archive)}]}
            receipt_path = root / "receipt.json"
            receipt_path.write_text(json.dumps(receipt))
            summary = {"receipt_sha256":catalog.digest(receipt_path), "verified_sources":[
                {"source":"wine","revision":"a"*40,"archive_sha256":catalog.digest(archive)},
                {"source":"project","revision":"working-tree@test"}]}
            catalog.validate_receipt(manifest,manifest_path,receipt_path,summary,root)
            for field, value in (("receipt_sha256","0"*64),("verified_sources",[])):
                bad = dict(summary)
                bad[field] = value
                with self.assertRaises(ValueError):
                    catalog.validate_receipt(manifest,manifest_path,receipt_path,bad,root)
            archive.write_bytes(b"changed archive")
            with self.assertRaises(ValueError):
                catalog.validate_receipt(manifest,manifest_path,receipt_path,summary,root)

    def test_changed_binary_retires_positive_evidence(self):
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in ("source.c","provider.dll","test.exe","marker.dll"):
                (root/name).write_bytes(b"original")
            sha=catalog.digest(root/"source.c")
            record={"id":"test-receipt","full_compatibility_verified":False,
                "receipt":{"exit_code":0,"timed_out":False,"output_truncated":False,
                    "output":"PASS\n","output_bytes":5,"output_sha256":hashlib.sha256(b"PASS\n").hexdigest()},
                "scope":"focused contract", "limitations":"other modes untested",
                "source_hashes":{"source.c":sha},
                "provider":{"path":"provider.dll","sha256":sha}, "test":{"path":"test.exe","sha256":sha},
                "supporting_artifacts":[{"path":"marker.dll","sha256":sha}],
                "apis":[{"dll":"KERNEL32.DLL","name":"InitOnceComplete"}],
                "evidence_kind":"guest_static_import_contract_subset", "documentation":"doc.md"}
            evidence={"schema":"w98mod.api-guest-evidence.v1","records":[record]}
            rows=catalog.merge_records([], [self.upstream()], {}, {}, self.groups)
            catalog.attach_evidence(rows,evidence,root)
            self.assertEqual(rows[0]["behavioral_coverage"],"guest_static_subset_verified")
            self.assertFalse(rows[0]["full_compatibility_verified"])
            (root/"marker.dll").write_bytes(b"changed supporting fixture")
            catalog.attach_evidence(rows,evidence,root)
            self.assertEqual(rows[0]["behavioral_coverage"],"historical_guest_subset_evidence")
            (root/"marker.dll").write_bytes(b"original")
            (root/"provider.dll").write_bytes(b"changed")
            catalog.attach_evidence(rows,evidence,root)
            self.assertEqual(rows[0]["behavioral_coverage"],"historical_guest_subset_evidence")
            record["receipt"]["exit_code"]=1
            with self.assertRaises(ValueError):
                catalog.attach_evidence(rows,evidence,root)


if __name__ == "__main__":
    unittest.main()
