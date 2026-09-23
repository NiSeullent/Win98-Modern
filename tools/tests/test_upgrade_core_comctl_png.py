"""Guarded earlier-to-v3 COMCTL provider CORE.INI upgrade tests."""
from __future__ import annotations

import hashlib
from pathlib import Path
import tempfile
import unittest

from tools.upgrade_core_comctl_png import prepare, upgrade


CORE = (b"[ApiConfigurations]\r\ndefault=0\r\n"
        b"[DCFG1]\r\ncontents=std,kexbases,m98ctl1\n"
        b"desc=Default\r\n"
        b"[DCFG1.ordinals.98]\r\n"
        b"COMCTL32.345=m98ctl1.0\r\n"
        b"COMCTL32.381=m98ctl1.0\r\n"
        b"KERNEL32.99=other.0\r\n"
        b"[DCFG1.ordinals.Me]\r\n"
        b"COMCTL32.345=m98ctl1.0\r\n"
        b"COMCTL32.381=m98ctl1.0\r\n"
        b"[WINXP.ordinals]\r\n"
        b"COMCTL32.345=m98ctl1.0\r\n"
        b"COMCTL32.381=m98ctl1.0\r\n")


class UpgradeTests(unittest.TestCase):
    def test_only_seven_references_change_and_newline_styles_survive(self) -> None:
        for previous in ("m98ctl1", "m98ctl2"):
            source = CORE.replace(b"m98ctl1", previous.encode())
            result = upgrade(source, previous)
            self.assertEqual(result, source.replace(previous.encode(), b"m98ctl3"))
            self.assertEqual(result.count(b"m98ctl3"), 7)
            with self.assertRaisesRegex(ValueError, "already referenced"):
                upgrade(result, previous)

    def test_rejects_changed_missing_and_duplicate_routes(self) -> None:
        with self.assertRaisesRegex(ValueError, "route changed"):
            upgrade(CORE.replace(b"COMCTL32.381=m98ctl1.0",
                                 b"COMCTL32.381=other.0", 1)
                    + b"; m98ctl1\r\n", "m98ctl1")
        with self.assertRaisesRegex(ValueError, "route count"):
            upgrade(CORE.replace(b"COMCTL32.381=m98ctl1.0\r\n",
                                 b"COMCTL32.381=m98ctl1.0\r\n"
                                 b"COMCTL32.381=m98ctl1.0\r\n", 1),
                    "m98ctl1")
        with self.assertRaisesRegex(ValueError, "missing section"):
            upgrade(CORE.replace(b"[WINXP.ordinals]", b"[OTHER.ordinals]"),
                    "m98ctl1")
        with self.assertRaisesRegex(ValueError, "exactly seven"):
            upgrade(CORE + b"; m98ctl1 used elsewhere\r\n", "m98ctl1")
        with self.assertRaisesRegex(ValueError, "unsupported"):
            upgrade(CORE, "m98ctl3")

    def test_prepare_writes_verified_backup_and_rejects_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "CORE.INI"
            candidate = root / "candidate.ini"
            backup = root / "CORE.orig.bak"
            dll = root / "M98CTLP.DLL"
            source.write_bytes(CORE)
            dll.write_bytes(b"frozen provider fixture")
            source_hash = hashlib.sha256(CORE).hexdigest()
            dll_hash = hashlib.sha256(dll.read_bytes()).hexdigest()
            with self.assertRaisesRegex(ValueError, "source SHA-256 mismatch"):
                prepare(source, candidate, backup, dll, "0" * 64, "m98ctl1",
                        expected_provider_sha256=dll_hash)
            with self.assertRaisesRegex(ValueError, "DLL SHA-256 mismatch"):
                prepare(source, candidate, backup, dll, source_hash,
                        "m98ctl1", expected_provider_sha256="0" * 64)
            self.assertFalse(backup.exists())
            self.assertFalse(candidate.exists())
            result = prepare(source, candidate, backup, dll, source_hash,
                             "m98ctl1", expected_provider_sha256=dll_hash)
            self.assertEqual(source.read_bytes(), CORE)
            self.assertEqual(backup.read_bytes(), CORE)
            self.assertEqual(candidate.read_bytes(), upgrade(CORE, "m98ctl1"))
            self.assertEqual(result["source_sha256"], result["backup_sha256"])
            self.assertEqual(result["routes"], 6)
            with self.assertRaisesRegex(ValueError, "must be new"):
                prepare(source, candidate, backup, dll, source_hash,
                        "m98ctl1", expected_provider_sha256=dll_hash)


if __name__ == "__main__":
    unittest.main()
