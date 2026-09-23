import unittest

from remote.patch_core_advapi import patch


SOURCE = (
    b"[DCFG1]\r\ncontents=std,kexbases,kexbasen,m98wrp3,m98shl3\r\n"
    b"[DCFG1.names.98]\r\nSHELL32.SHParseDisplayName=m98shl3.0\r\n"
    b"[DCFG1.names.Me]\r\nSHELL32.SHParseDisplayName=m98shl3.0\r\n"
    b"[WINXP.names]\r\nSHELL32.SHParseDisplayName=m98shl3.0\r\n"
)


class TestPatch(unittest.TestCase):
    def test_exact_insertions_and_crlf(self):
        result = patch(SOURCE)
        self.assertEqual(result.count(b"ADVAPI32.RegGetValueW=m98adv.0\r\n"), 3)
        self.assertIn(b"contents=std,kexbases,kexbasen,m98wrp3,m98shl3,m98adv\r\n", result)
        self.assertEqual(result.replace(b"ADVAPI32.RegGetValueW=m98adv.0\r\n", b"").replace(b",m98adv\r\n", b"\r\n"), SOURCE)

    def test_reject_duplicate_or_missing_anchor(self):
        with self.assertRaises(ValueError):
            patch(patch(SOURCE))
        with self.assertRaises(ValueError):
            patch(SOURCE.replace(b"[WINXP.names]\r\nSHELL32.SHParseDisplayName=m98shl3.0\r\n", b"[WINXP.names]\r\n"))

    def test_reject_long_stem(self):
        with self.assertRaises(ValueError):
            patch(SOURCE, "m98advapi")


if __name__ == "__main__":
    unittest.main()
