"""Strict route and byte-preservation checks for wrapper upgrades."""

import unittest

from remote.patch_core_wrapper_upgrade import patch

API_NAMES = {
    b"CompareStringEx", b"LCMapStringEx", b"CloseThreadpoolWork",
    b"CreateThreadpoolWork", b"FreeLibraryWhenCallbackReturns",
    b"SubmitThreadpoolWork", b"WaitForThreadpoolWorkCallbacks",
    b"InitOnceBeginInitialize", b"InitOnceComplete", b"InitOnceExecuteOnce",
    b"InitOnceInitialize",
}


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
        self.assertEqual(updated.count(b"m98wrp14"), 34)

    def test_rejects_missing_or_wrong_routes(self) -> None:
        broken = self.sample().replace(b"KERNEL32.SubmitThreadpoolWork=m98wrp13.0\r\n", b"", 1)
        with self.assertRaises(ValueError):
            patch(broken, "m98wrp13", "m98wrp14")

    def test_discovers_future_families_and_preserves_table_index(self) -> None:
        original = self.sample().replace(b"OTHER.Entry=untouched\r\n",
            b"NTDLL.RtlFirstEntrySList=m98wrp13.1\r\nOTHER.Entry=untouched\r\n")
        original = b";Korean byte \x81 and m98wrp13 commentary\r\n" + original
        result = patch(original, "m98wrp13", "m98wrp17")
        self.assertEqual(result.count(b"NTDLL.RtlFirstEntrySList=m98wrp17.1"), 3)
        self.assertIn(b";Korean byte \x81 and m98wrp13 commentary", result)
        self.assertNotIn(b"=m98wrp13.", result)
        self.assertEqual(result.replace(b"m98wrp17", b"m98wrp13"), original)

    def test_rejects_ambiguous_and_stranded_references(self) -> None:
        original = self.sample()
        invalid = (
            original + b"[WINXP.names]\r\n",
            original.replace(b"OTHER.Entry=untouched", b"KERNEL32.InitOnceComplete=other", 1),
            original.replace(b"=m98wrp13.0", b"=m98wrp13.1", 1),
            original + b"[unexpected]\r\nKERNEL32.Future=m98wrp13.0\r\n",
            original.replace(b"=m98wrp13.0", b"=m98wrp13.0 ", 1),
            original.replace(b"KERNEL32.CompareStringEx=m98wrp13.0",
                             b"KERNEL32.CompareStringEx=shadow=m98wrp13.0"),
            original.replace(b"[WINXP.names]", b"[wrong]"),
            original.replace(b"contents=kexbases", b"contents=m98wrp14,kexbases"),
        )
        for data in invalid:
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    patch(data, "m98wrp13", "m98wrp14")
        broken = self.sample().replace(b"m98wrp13.0", b"kexbases.0", 1)
        with self.assertRaises(ValueError):
            patch(broken, "m98wrp13", "m98wrp14")


if __name__ == "__main__":
    unittest.main()
