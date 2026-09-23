"""Send a command to this VM's local QEMU monitor."""

import argparse
import socket
import time


parser = argparse.ArgumentParser()
parser.add_argument("command", nargs="+")
parser.add_argument("--quiet", action="store_true")
args = parser.parse_args()

with socket.create_connection(("127.0.0.1", 4545), timeout=5) as connection:
    connection.settimeout(2)
    connection.recv(65536)  # Initial QEMU greeting and prompt.
    connection.sendall((" ".join(args.command) + "\r\n").encode("ascii"))
    time.sleep(0.25)
    chunks = []
    while True:
        try:
            chunk = connection.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
            if b"(qemu)" in chunk:
                break
        except socket.timeout:
            break

if not args.quiet:
    print(b"".join(chunks).decode("utf-8", errors="replace"))
