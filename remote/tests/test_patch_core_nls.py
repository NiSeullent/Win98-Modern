import unittest

from remote.patch_core_nls import patch


class PatchNlsRoutesTest(unittest.TestCase):
    def setUp(self):
        self.source = (
            b"[DCFG1]\r\ncontents=std,kexbases,m98wrp12,m98ad2\r\n"
            b"[DCFG1.names.98]\r\nADVAPI32.RegGetValueW=m98ad2.0\r\n"
            b"[DCFG1.names.Me]\r\nADVAPI32.RegGetValueW=m98ad2.0\r\n"
            b"[WINXP.names]\r\nADVAPI32.RegGetValueW=m98ad2.0\r\n"
        )

    def test_preserves_bytes_and_adds_routes_to_each_profile(self):
        result = patch(self.source, "m98wrp12")
        self.assertEqual(result.count(b"KERNEL32.CompareStringEx=m98wrp12.0\r\n"), 3)
        self.assertEqual(result.count(b"KERNEL32.LCMapStringEx=m98wrp12.0\r\n"), 3)
        self.assertEqual(result.replace(
            b"KERNEL32.CompareStringEx=m98wrp12.0\r\n", b""
        ).replace(b"KERNEL32.LCMapStringEx=m98wrp12.0\r\n", b""), self.source)

    def test_rejects_duplicate_and_unexpected_provider(self):
        with self.assertRaisesRegex(ValueError, "already exists"):
            patch(patch(self.source, "m98wrp12"), "m98wrp12")
        with self.assertRaisesRegex(ValueError, "provider"):
            patch(self.source, "m98wrp13")


if __name__ == "__main__":
    unittest.main()
