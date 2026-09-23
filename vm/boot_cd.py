"""Reset the VM and select CD-ROM in the Windows 98 startup menu."""

import socket
import time


with socket.create_connection(("127.0.0.1", 4545), timeout=5) as connection:
    connection.settimeout(5)
    connection.recv(65536)
    connection.sendall(b"system_reset\r\n")
    time.sleep(2)
    connection.sendall(b"sendkey 2\r\n")
    time.sleep(0.2)
    connection.sendall(b"sendkey ret\r\n")
