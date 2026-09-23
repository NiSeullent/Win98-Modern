import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from patch_core_family import patch, select_family


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
                    data+b"[WINXP.names]\r\n", data.replace(b"[WINXP.names]",b"[other]"),
                    data+b"[DCFG1]\r\n", data.replace(b"KERNEL32.Init",b" KERNEL32.Init",1),
                    data.replace(b"Complete=",b"Complete =",1)):
            with self.assertRaises(ValueError):
                patch(bad,"m98wrp16",["InitOnceComplete"])

    def test_separate_user_provider_registration_preserves_kernel_routes(self):
        original=patch(self.baseline(),"m98wrp16",["InitOnceComplete"])
        result=patch(original,"m98user1",["AddClipboardFormatListener"],
                     "USER32.DLL",2,register=True)
        route=b"USER32.AddClipboardFormatListener=m98user1.2\r\n"
        self.assertEqual(result.count(route),3)
        restored=result.replace(route,b"").replace(b"contents=std,m98wrp16,m98user1\r\n",
                                                   b"contents=std,m98wrp16\r\n")
        self.assertEqual(restored,original)
        self.assertEqual(patch(result,"m98user1",["AddClipboardFormatListener"],
                               "USER32.DLL",2,register=True),result)
        with self.assertRaises(ValueError):
            patch(original,"m98user1",["AddClipboardFormatListener"],"USER32.DLL")

    def test_target_identity_and_table_conflicts_rejected(self):
        for dll,table in (("USER32",0),("user32.dll",0),("USER32.DLL",-1),
                          ("USER32.DLL",True),("USER32.DLL",256)):
            with self.assertRaises(ValueError):
                patch(self.baseline(),"m98wrp16",["Foo"],dll,table)
        result=patch(self.baseline(),"m98wrp16",["Foo"],"USER32.DLL",1)
        with self.assertRaises(ValueError):
            patch(result,"m98wrp16",["Foo"],"USER32.DLL",0)

    def test_all_never_mixes_dlls_and_target_mismatch_rejected(self):
        manifest={"schema":"w98mod.runtime-routes.v2","families":{
            "kernel":{"dll":"KERNEL32.DLL","table":0,"names":["A","B"]},
            "kernel2":{"dll":"KERNEL32.DLL","table":0,"names":["B","C"]},
            "clipboard":{"dll":"USER32.DLL","table":1,"names":["A"]}}}
        self.assertEqual(select_family(manifest,"all"),("KERNEL32.DLL",0,["A","B","C"]))
        self.assertEqual(select_family(manifest,"clipboard"),("USER32.DLL",1,["A"]))
        self.assertEqual(select_family(manifest,"all","USER32.DLL"),("USER32.DLL",1,["A"]))
        for family,dll in (("clipboard","KERNEL32.DLL"),("absent",None),("all","GDI32.DLL")):
            with self.assertRaises(ValueError):
                select_family(manifest,family,dll)
        manifest["families"]["kernel2"]["table"]=2
        with self.assertRaises(ValueError):
            select_family(manifest,"all")


if __name__=="__main__":
    unittest.main()
