"""Type short ASCII text into the running guest through the local monitor."""

import argparse
import getpass
import socket
import time


parser = argparse.ArgumentParser()
parser.add_argument("text", nargs="?")
parser.add_argument("--secret", action="store_true")
args = parser.parse_args()

if args.secret:
    value = getpass.getpass("Guest text: ")
elif args.text is not None:
    value = args.text
else:
    parser.error("provide text or --secret")

special = {
    "-": "minus",
    ":": "shift-semicolon",
    "\\": "backslash",
    "/": "slash",
    ".": "dot",
    "_": "shift-minus",
    " ": "spc",
    "\t": "tab",
}

with socket.create_connection(("127.0.0.1", 4545), timeout=5) as connection:
    connection.settimeout(2)
    connection.recv(65536)
    for character in value:
        key = special.get(character, character.lower())
        if not (len(key) == 1 and key.isalnum()) and character not in special:
            raise ValueError(f"Unsupported character: U+{ord(character):04X}")
        connection.sendall(f"sendkey {key}\r\n".encode("ascii"))
        time.sleep(0.08)
        try:
            connection.recv(65536)
        except socket.timeout:
            pass
