"""Build a bootable, read-only E820 probe floppy for the disposable VMs.

ShizukuDOS is built from this repository's sources. No Windows or third-party
DOS binaries are included. At its prompt run: EXEC E820.COM
"""

from __future__ import annotations

from pathlib import Path
import hashlib
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "vm" / "accel" / "memory-tests" / "e820-probe-floppy.img"


def main() -> None:
    assembler = shutil.which("nasm")
    if assembler is None:
        raise SystemExit("NASM is required to build the E820 floppy")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        raise SystemExit(f"Probe floppy already exists; refusing to replace it: {OUTPUT}")

    sys.path.insert(0, str(ROOT))
    from shizukudos.build_image import make_image

    sources = {
        "boot": ROOT / "shizukudos" / "boot.asm",
        "stage2": ROOT / "shizukudos" / "stage2.asm",
        "e820": ROOT / "memory" / "e820.asm",
    }
    binaries: dict[str, bytes] = {}
    with TemporaryDirectory(prefix="e820-floppy-") as temporary:
        for name, source in sources.items():
            destination = Path(temporary) / f"{name}.bin"
            subprocess.run([assembler, "-f", "bin", str(source), "-o", str(destination)], check=True)
            binaries[name] = destination.read_bytes()

    image = make_image(
        binaries["boot"],
        binaries["stage2"],
        {
            "README.TXT": b"ShizukuDOS E820 probe. Run EXEC E820.COM.\r\n",
            "E820.COM": binaries["e820"],
        },
    )
    temporary_output = OUTPUT.with_suffix(".img.partial")
    temporary_output.write_bytes(image)
    temporary_output.replace(OUTPUT)
    digest = hashlib.sha256(image).hexdigest().upper()
    print(f"{OUTPUT} ({len(image)} bytes, SHA-256 {digest})")
    print("At the ShizukuDOS prompt: EXEC E820.COM")


if __name__ == "__main__":
    main()
