import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from patch_core_family import patch


class FamilyRouteTests(unittest.TestCase):
    def baseline(self):
        return b";keep\x81\r\n[DCFG1]\r\ncontents=std,m98wrp16\r\n" + b"".join(
            b"["+s+b"]\r\nKERNEL32.Other=std\r\n" for s in
            (b"DCFG1.names.98",b"DCFG1.names.Me",b"WINXP.names"))

    def test_exact_additions_and_idempotence(self):
        data=self.baseline()
        result=patch(data,"m98wrp16",["InitOnceComplete"])
        route=b"KERNEL32.InitOnceComplete=m98wrp16.0\r\n"
        self.assertEqual(result.count(route),3)
        self.assertEqual(result.replace(route,b""),data)
        self.assertEqual(patch(result,"m98wrp16",["InitOnceComplete"]),result)

    def test_conflicting_duplicate_and_missing_profiles_rejected(self):
        data=patch(self.baseline(),"m98wrp16",["InitOnceComplete"])
        for bad in (data.replace(b"Complete=m98wrp16",b"Complete=other",1),
                    data+b"[WINXP.names]\r\n", data.replace(b"[WINXP.names]",b"[other]")):
            with self.assertRaises(ValueError):
                patch(bad,"m98wrp16",["InitOnceComplete"])


if __name__=="__main__":
    unittest.main()
