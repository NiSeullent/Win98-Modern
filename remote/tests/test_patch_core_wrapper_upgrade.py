"""Strict route and byte-preservation checks for wrapper upgrades."""

import unittest

from remote.patch_core_wrapper_upgrade import API_NAMES, patch


class WrapperUpgradeTests(unittest.TestCase):
    def sample(self) -> bytes:
        data = b"[DCFG1]\r\ncontents=kexbases,m98wrp13,kexbasen\r\n"
        for section in (b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"):
            data += b"[" + section + b"]\r\n"
            for name in sorted(API_NAMES):
                data += b"KERNEL32." + name + b"=m98wrp13.0\r\n"
            data += b"OTHER.Entry=untouched\r\n"
        return data

    def test_upgrades_exact_occurrences(self) -> None:
        original = self.sample()
        updated = patch(original, "m98wrp13", "m98wrp14")
        self.assertEqual(updated, original.replace(b"m98wrp13", b"m98wrp14"))
        self.assertEqual(updated.count(b"m98wrp14"), 22)

    def test_rejects_missing_or_wrong_routes(self) -> None:
        broken = self.sample().replace(b"KERNEL32.SubmitThreadpoolWork=m98wrp13.0\r\n", b"", 1)
        with self.assertRaises(ValueError):
            patch(broken, "m98wrp13", "m98wrp14")
        broken = self.sample().replace(b"m98wrp13.0", b"kexbases.0", 1)
        with self.assertRaises(ValueError):
            patch(broken, "m98wrp13", "m98wrp14")


if __name__ == "__main__":
    unittest.main()
