"""Paced console bootstrap keys for a disposable lab VM before COM1 is ready."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import time

VBOX = Path(r"C:\Program Files\Oracle\VirtualBox\VBoxManage.exe")
KEYS = {
    "win": (["e0", "5b"], ["e0", "db"]),
    "u": (["16"], ["96"]),
    "r": (["13"], ["93"]),
    "enter": (["1c"], ["9c"]),
    "escape": (["01"], ["81"]),
    "up": (["e0", "48"], ["e0", "c8"]),
    "down": (["e0", "50"], ["e0", "d0"]),
    "tab": (["0f"], ["8f"]),
    "alt-f4": (["38", "3e"], ["be", "b8"]),
    "alt-enter": (["38", "1c"], ["9c", "b8"]),
    "ctrl-alt-delete": (["1d", "38", "e0", "53"], ["e0", "d3", "b8", "9d"]),
    "win-r": (["e0", "5b", "13"], ["93", "e0", "db"]),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vm", default="Win98Modern-Accel-128")
    operations = parser.add_subparsers(dest="action", required=True)
    operations.add_parser("key").add_argument("name", choices=KEYS)
    operations.add_parser("run").add_argument("words", nargs=argparse.REMAINDER)
    operations.add_parser("type").add_argument("words", nargs=argparse.REMAINDER)
    operations.add_parser("run-file").add_argument("path", type=Path)
    operations.add_parser("type-file").add_argument("path", type=Path)
    operations.add_parser("shot").add_argument("path", type=Path)
    args = parser.parse_args()
    local_preview = False
    if re.fullmatch(r"Win98-Shizuku-SE-Preview-[A-Za-z0-9_.-]+", args.vm):
        manifest = (Path(__file__).resolve().parents[1] / "prebuilt" / "local" /
                    args.vm / "prebuilt-manifest.json")
        try:
            local_preview = json.loads(manifest.read_text(encoding="utf-8"))["clone"]["vm_name"] == args.vm
        except (OSError, ValueError, KeyError, TypeError):
            pass
    if args.vm == "Win98Modern-Base" or not (args.vm.startswith("Win98Modern-") or local_preview):
        parser.error("use a disposable Win98Modern VM or a manifested local preview")

    def control(*values):
        subprocess.run([str(VBOX), "controlvm", args.vm, *values], check=True, timeout=20)

    def key(name):
        make, release = KEYS[name]
        control("keyboardputscancode", *make)
        time.sleep(0.15)
        control("keyboardputscancode", *release)
        time.sleep(0.6)

    if args.action == "key":
        key(args.name)
    elif args.action in ("run", "type", "run-file", "type-file"):
        command = args.path.read_text(encoding="ascii").rstrip("\r\n") if args.action.endswith("-file") else " ".join(args.words)
        if not command or not command.isascii() or "\n" in command or "\r" in command:
            parser.error("bootstrap keyboard commands must be one ASCII line")
        if args.action.startswith("run"):
            key("win-r")
        control("keyboardputstring", command)
        time.sleep(0.5)
        key("enter")
    else:
        control("screenshotpng", str(args.path.resolve()))


if __name__ == "__main__":
    main()
