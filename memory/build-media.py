"""Build a data CD containing the Win98 and real-mode memory probes.

The output stays in vm/accel/memory-tests, outside Git. It includes no OS media.
Requires NASM and pycdlib (tools/requirements-test-media.txt).
"""

from __future__ import annotations

from io import BytesIO
from pathlib import Path
import shutil
import subprocess

import pycdlib


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "vm" / "accel" / "memory-tests" / "memory-probes.iso"
COM = OUTPUT.parent / "e820.com"
FILES = (
    (ROOT / "build" / "memprobe.exe", "/MEMPROBE.EXE;1", "/MEMPROBE.EXE"),
    (ROOT / "build" / "memstress.exe", "/MEMSTRS.EXE;1", "/MEMSTRS.EXE"),
    (COM, "/E820.COM;1", "/E820.COM"),
)


def main() -> None:
    assembler = shutil.which("nasm")
    if assembler is None:
        raise SystemExit("NASM is required to build the DOS E820 probe")
    for path, _, _ in FILES[:2]:
        if not path.is_file():
            raise SystemExit(f"Build the Win98 probes first: {path}")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        raise SystemExit(f"Probe CD already exists; refusing to replace it: {OUTPUT}")
    subprocess.run(
        [assembler, "-f", "bin", str(ROOT / "memory" / "e820.asm"), "-o", str(COM)],
        check=True,
    )
    if COM.stat().st_size > 65536 - 256:
        raise SystemExit("E820 COM program is too large")
    temporary = OUTPUT.with_suffix(".iso.partial")
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, joliet=3, vol_ident="MEMTEST")
    try:
        for source, iso_path, joliet_path in FILES:
            iso.add_file(str(source), iso_path=iso_path, joliet_path=joliet_path)
        iso.write(str(temporary))
    finally:
        iso.close()
    temporary.replace(OUTPUT)
    check = pycdlib.PyCdlib()
    check.open(str(OUTPUT))
    try:
        for source, _, joliet_path in FILES:
            payload = BytesIO()
            check.get_file_from_iso_fp(payload, joliet_path=joliet_path)
            if payload.getvalue() != source.read_bytes():
                raise RuntimeError(f"CD payload mismatch: {joliet_path}")
    finally:
        check.close()
    print(OUTPUT)
    for _, iso_path, _ in FILES:
        print(iso_path)


if __name__ == "__main__":
    main()
