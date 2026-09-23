"""Capture the local QEMU VM screen as a PNG for visual inspection."""

from pathlib import Path
import socket
import time

from PIL import Image


vm_directory = Path(__file__).parent
ppm = vm_directory / "screen.ppm"
png = vm_directory / "screen.png"

with socket.create_connection(("127.0.0.1", 4545), timeout=5) as connection:
    connection.settimeout(2)
    connection.recv(65536)
    connection.sendall((f"screendump {ppm.as_posix()}\r\n").encode("ascii"))
    time.sleep(0.4)

with Image.open(ppm) as image:
    image.save(png)

print(png)
