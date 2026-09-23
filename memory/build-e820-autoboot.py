"""Build a keyboard-free BIOS E820 floppy image from e820_boot.asm."""

from __future__ import annotations

from pathlib import Path
import hashlib
import shutil
import subprocess
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "vm" / "accel" / "memory-tests" / "e820-autoboot.img"


def main() -> None:
    assembler = shutil.which("nasm")
    if assembler is None:
        raise SystemExit("NASM is required to build the E820 boot floppy")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        raise SystemExit(f"Probe floppy already exists; refusing to replace it: {OUTPUT}")
    with TemporaryDirectory(prefix="e820-autoboot-") as temporary:
        boot = Path(temporary) / "boot.bin"
        subprocess.run(
            [assembler, "-f", "bin", str(ROOT / "memory" / "e820_boot.asm"), "-o", str(boot)],
            check=True,
        )
        sector = boot.read_bytes()
    if len(sector) != 512 or sector[-2:] != b"\x55\xaa":
        raise SystemExit("E820 boot sector has an invalid size or signature")
    image = sector + bytes(1474560 - 512)
    temporary_output = OUTPUT.with_suffix(".img.partial")
    temporary_output.write_bytes(image)
    temporary_output.replace(OUTPUT)
    print(f"{OUTPUT} (SHA-256 {hashlib.sha256(image).hexdigest().upper()})")


if __name__ == "__main__":
    main()
