import unittest

from remote.patch_core_advapi_upgrade import patch


CORE = (
    b"[DCFG1]\r\ncontents=std,kexbases,m98adv,m98wrp9\r\n"
    b"[DCFG1.names.98]\r\nADVAPI32.RegGetValueW=m98adv.0\r\nKERNEL32.X=std\r\n"
    b"[DCFG1.names.Me]\r\nADVAPI32.RegGetValueW=m98adv.0\r\n"
    b"[WINXP.names]\r\nADVAPI32.RegGetValueW=m98adv.0\r\n"
)


class UpgradeTests(unittest.TestCase):
    def test_exact_upgrade_and_unrelated_bytes(self):
        updated = patch(CORE)
        self.assertEqual(updated.count(b"RegGetValueA=m98ad2.0"), 3)
        self.assertEqual(updated.count(b"RegGetValueW=m98ad2.0"), 3)
        self.assertIn(b"contents=std,kexbases,m98ad2,m98wrp9\r\n", updated)
        self.assertIn(b"KERNEL32.X=std\r\n", updated)
        self.assertEqual(updated.count(b"\n"), CORE.count(b"\n") + 3)

    def test_duplicate_or_wrong_routes_rejected(self):
        with self.assertRaises(ValueError):
            patch(CORE.replace(b"m98adv.0", b"other.0", 1))
        with self.assertRaises(ValueError):
            patch(CORE.replace(b"KERNEL32.X=std", b"ADVAPI32.RegGetValueA=m98adv.0"))
        with self.assertRaises(ValueError):
            patch(CORE + b"[DCFG1.names.98]\r\nADVAPI32.RegGetValueW=m98adv.0\r\n")


if __name__ == "__main__":
    unittest.main()
