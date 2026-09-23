"""Byte preservation and rejection tests for COMCTL32 ordinal CORE routes."""
from __future__ import annotations

import unittest

from tools.patch_core_ordinals import patch


CORE = (b"[ApiConfigurations]\r\ndefault=0\r\n"
        b"[DCFG1]\r\ncontents=std,kexbases\ndesc=Default\r\n"
        b"[DCFG1.names.98]\r\nKERNEL32.Foo=std\r\n"
        b"[DCFG1.names.Me]\r\nKERNEL32.Foo=std\r\n"
        b"[WINXP.names]\r\nKERNEL32.Foo=std\r\n")


class OrdinalPatchTests(unittest.TestCase):
    def test_routes_and_preservation(self) -> None:
        result = patch(CORE, "m98ctl1")
        self.assertIn(b"contents=std,kexbases,m98ctl1\n", result)
        for heading in (b"DCFG1.ordinals.98", b"DCFG1.ordinals.Me",
                        b"WINXP.ordinals"):
            self.assertIn(b"[" + heading + b"]\r\n"
                          b"COMCTL32.345=m98ctl1.0\r\n"
                          b"COMCTL32.381=m98ctl1.0\r\n", result)
        stripped = result.replace(b",m98ctl1", b"")
        for heading in (b"DCFG1.ordinals.98", b"DCFG1.ordinals.Me",
                        b"WINXP.ordinals"):
            stripped = stripped.replace(b"[" + heading + b"]\r\n"
                                        b"COMCTL32.345=m98ctl1.0\r\n"
                                        b"COMCTL32.381=m98ctl1.0\r\n\r\n", b"")
        self.assertEqual(stripped, CORE)

    def test_rejects_duplicate_and_existing_routes(self) -> None:
        with self.assertRaisesRegex(ValueError, "already registered"):
            patch(patch(CORE, "m98ctl1"), "m98ctl1")
        with self.assertRaisesRegex(ValueError, "existing ordinal section"):
            patch(CORE + b"[WINXP.ordinals]\r\nCOMCTL32.381=other.0\r\n",
                  "m98ctl1")

    def test_rejects_missing_and_bad_provider(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing profile"):
            patch(CORE.replace(b"[WINXP.names]", b"[CUSTOM.names]"), "m98ctl1")
        with self.assertRaisesRegex(ValueError, "DOS 8.3"):
            patch(CORE, "invalid_long")


if __name__ == "__main__":
    unittest.main()
