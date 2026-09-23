"""Build the two-file, non-bootable remote-agent CD; verify both payloads."""

from io import BytesIO
from pathlib import Path
import hashlib
import json

import pycdlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = ROOT / "vm/remote-bootstrap.iso"
    # COMMAND.COM needs DOS line endings even when checkout uses LF.
    batch = (ROOT / "remote/BOOTLAB.BAT").read_text(encoding="ascii")
    files = {"M98AGENT.EXE": (ROOT / "build/m98agent.exe").read_bytes(),
             "BOOTLAB.BAT": batch.replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii")}
    temporary = output.with_suffix(".iso.partial")
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, joliet=3, vol_ident="M98REMOTE")
    streams = []
    try:
        iso.add_directory(iso_path="/LAB", joliet_path="/LAB")
        for name, data in files.items():
            stream = BytesIO(data)
            streams.append(stream)
            iso.add_fp(stream, len(data), iso_path="/LAB/" + name + ";1", joliet_path="/LAB/" + name)
        iso.write(str(temporary))
    finally:
        iso.close()
        for stream in streams:
            stream.close()
    check = pycdlib.PyCdlib()
    check.open(str(temporary))
    try:
        for name, data in files.items():
            for kind, path in (("iso_path", "/LAB/" + name + ";1"), ("joliet_path", "/LAB/" + name)):
                actual = BytesIO()
                check.get_file_from_iso_fp(actual, **{kind: path})
                if actual.getvalue() != data:
                    raise RuntimeError("bootstrap CD payload mismatch: " + name)
    finally:
        check.close()
    temporary.replace(output)
    print(json.dumps({"iso": str(output), "files": {
        name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for name, data in files.items()}}, indent=2))


if __name__ == "__main__":
    main()
