"""Byte-preservation and route-count checks for threadpool CORE.INI edits."""

import unittest

from remote.patch_core_threadpool import NAMES, patch


class ThreadpoolPatchTests(unittest.TestCase):
    def sample(self) -> bytes:
        data = b"[DCFG1]\r\ncontents=kexbases,m98wrp13,kexbasen\r\n"
        for section in (b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"):
            data += b"[" + section + b"]\r\n"
            data += b"KERNEL32.LCMapStringEx=m98wrp13.0\r\n"
            data += b"OTHER.Entry=untouched\r\n"
        return data

    def test_inserts_only_expected_routes(self) -> None:
        original = self.sample()
        updated = patch(original, "m98wrp13")
        self.assertEqual(updated.count(b"=m98wrp13.0"), 18)
        self.assertEqual(updated.count(b"OTHER.Entry=untouched"), 3)
        for name in NAMES:
            self.assertEqual(updated.count(b"KERNEL32." + name + b"=m98wrp13.0"), 3)

    def test_rejects_duplicates(self) -> None:
        with self.assertRaises(ValueError):
            patch(patch(self.sample(), "m98wrp13"), "m98wrp13")


if __name__ == "__main__":
    unittest.main()
