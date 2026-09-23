"""Regression checks for byte-preserving installed CORE.INI NLS upgrades."""

import unittest

from remote.patch_core_nls_upgrade import patch


class NlsUpgradeTests(unittest.TestCase):
    def sample(self) -> bytes:
        names = (b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names")
        data = b"; unchanged\r\n[DCFG1]\r\ncontents=kexbases,m98wrp12,kexbasen\r\n"
        for section in names:
            data += b"[" + section + b"]\r\n"
            data += b"KERNEL32.CompareStringEx=m98wrp12.0\r\n"
            data += b"KERNEL32.LCMapStringEx=m98wrp12.0\r\n"
            data += b"OTHER.Entry=keep.0\r\n"
        return data

    def test_only_expected_bytes_change(self) -> None:
        original = self.sample()
        updated = patch(original, "m98wrp12", "m98wrp13")
        self.assertEqual(updated, original.replace(b"m98wrp12", b"m98wrp13"))
        self.assertEqual(updated.count(b"m98wrp13"), 7)

    def test_rejects_wrong_or_missing_routes(self) -> None:
        with self.assertRaises(ValueError):
            patch(self.sample().replace(b"m98wrp12.0", b"kexbases.0", 1),
                  "m98wrp12", "m98wrp13")
        with self.assertRaises(ValueError):
            patch(self.sample().replace(b"KERNEL32.LCMapStringEx=m98wrp12.0\r\n", b"", 1),
                  "m98wrp12", "m98wrp13")


if __name__ == "__main__":
    unittest.main()
