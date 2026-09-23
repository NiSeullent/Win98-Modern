"""Check that KernelEx configuration patching preserves non-ASCII bytes."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "patch-core-ini.ps1"
BUILD = (ROOT / "build").resolve()


def patch(source: Path, target: Path) -> None:
    result = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(SCRIPT),
            "-InputPath",
            str(source),
            "-OutputPath",
            str(target),
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise AssertionError(result.stderr or result.stdout)


def expect_patch_failure(source: Path, target: Path) -> None:
    result = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(SCRIPT),
            "-InputPath",
            str(source),
            "-OutputPath",
            str(target),
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        raise AssertionError("invalid configuration was accepted")


def main() -> None:
    if not BUILD.is_relative_to(ROOT.resolve()):
        raise AssertionError("test directory escaped the project workspace")
    BUILD.mkdir(exist_ok=True)
    before = "[DCFG1]\r\n; 한글 설정\r\ncontents=std,kexbases,kexbasen\r\n[Other]\nlabel=한글\n"
    after = before.replace(
        "contents=std,kexbases,kexbasen",
        "contents=std,kexbases,kexbasen,m98wrap",
    )
    cases = (
        ("cp949", b"", "cp949"),
        ("utf8", b"", "utf-8"),
        ("utf8-bom", b"\xef\xbb\xbf", "utf-8"),
        ("utf16le-bom", b"\xff\xfe", "utf-16-le"),
        ("utf16be-bom", b"\xfe\xff", "utf-16-be"),
    )
    with tempfile.TemporaryDirectory(dir=BUILD) as directory:
        folder = Path(directory)
        for label, bom, codec in cases:
            source = folder / f"{label}.ini"
            target = folder / f"{label}.patched.ini"
            again = folder / f"{label}.again.ini"
            source.write_bytes(bom + before.encode(codec))
            patch(source, target)
            expected = bom + after.encode(codec)
            if target.read_bytes() != expected:
                raise AssertionError(f"{label}: bytes or line endings changed")
            patch(target, again)
            if again.read_bytes() != expected:
                raise AssertionError(f"{label}: patch was not idempotent")
            patch(source, source)
            if source.read_bytes() != expected:
                raise AssertionError(f"{label}: in-place patch changed other bytes")

        rejected = (
            ("missing-section", b"[Other]\ncontents=std\n"),
            ("duplicate-key", b"[DCFG1]\ncontents=std\ncontents=std\n"),
            ("invalid-utf8-bom", b"\xef\xbb\xbf[DCFG1]\ncontents=std\n;\xff\n"),
        )
        for label, original in rejected:
            source = folder / f"{label}.ini"
            target = folder / f"{label}.patched.ini"
            source.write_bytes(original)
            target.write_bytes(b"KEEP EXISTING CONFIG")
            expect_patch_failure(source, target)
            if target.read_bytes() != b"KEEP EXISTING CONFIG":
                raise AssertionError(f"{label}: rejected patch changed target")
            expect_patch_failure(source, source)
            if source.read_bytes() != original:
                raise AssertionError(f"{label}: rejected patch changed source")
        leftovers = list(folder.glob(".*.tmp*"))
        if leftovers:
            raise AssertionError(f"temporary patch files remain: {leftovers}")
    print("PASS: KernelEx config encodings, in-place updates, and rejection safety")


if __name__ == "__main__":
    main()
