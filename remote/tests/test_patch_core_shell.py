"""Guard against accidentally rewriting unrelated installed KernelEx settings."""

import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    "patch_core_shell", Path(__file__).resolve().parents[1] / "patch_core_shell.py"
)
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class InstalledCorePatchTests(unittest.TestCase):
    def test_adds_routes_without_changing_unrelated_bytes(self):
        original = (
            b"[DCFG1]\r\ncontents=std,kexbases,kexbasen,m98wrap,m98shl2\r\n"
            b"desc=\xc7\xd1\xb1\xdb\r\n[DCFG1.names.98]\r\n"
            b"SHELL32.SHParseDisplayName=m98shl2.0\r\n"
            b"[DCFG1.names.Me]\r\nSHELL32.SHParseDisplayName=m98shl2.0\r\n"
            b"[WINXP.names]\r\nSHELL32.SHParseDisplayName=m98shl2.0\r\n"
            b"other=keep\r\n"
        )
        changed = patcher.patch(original, "m98shl2", "m98shl3")
        self.assertIn(b"desc=\xc7\xd1\xb1\xdb\r\n", changed)
        self.assertTrue(changed.endswith(b"other=keep\r\n"))
        self.assertEqual(changed.count(b"SHELL32.SHOpenFolderAndSelectItems=m98shl3.0\r\n"), 3)
        self.assertEqual(changed.count(b"SHELL32.SHParseDisplayName=m98shl3.0\r\n"), 3)
        self.assertEqual(original.count(b"\r\n") + 3, changed.count(b"\r\n"))
        next_version = patcher.patch(changed, "m98shl3", "m98shl4")
        self.assertEqual(next_version.count(b"SHELL32.SHOpenFolderAndSelectItems=m98shl4.0\r\n"), 3)
        self.assertEqual(changed.count(b"\r\n"), next_version.count(b"\r\n"))

    def test_rejects_unexpected_existing_route(self):
        data = (
            b"[DCFG1]\ncontents=std,kexbases,kexbasen,m98wrap,m98shl2\n"
            b"[DCFG1.names.98]\nSHELL32.SHParseDisplayName=none\n"
            b"[DCFG1.names.Me]\nSHELL32.SHParseDisplayName=m98shl2.0\n"
            b"[WINXP.names]\nSHELL32.SHParseDisplayName=m98shl2.0\n"
        )
        with self.assertRaises(ValueError):
            patcher.patch(data, "m98shl2", "m98shl3")


if __name__ == "__main__":
    unittest.main()
