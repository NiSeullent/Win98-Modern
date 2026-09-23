"""Control the visible Win98 lab agent through a local VirtualBox serial pipe."""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import ntpath
import os
from pathlib import Path
import re
import struct
import time
import uuid

MAGIC = b"M98R"
VERSION = 1
HEADER = struct.Struct("<4sIIII")
MAX_PAYLOAD = 131072
CHUNK = 65536
REPLY = 0x80000000
DEFAULT_PIPE = r"\\.\pipe\Win98Modern-Lab"


class ProtocolError(RuntimeError):
    pass


class RemoteError(RuntimeError):
    def __init__(self, opcode: int, code: int):
        self.opcode, self.code = opcode, code
        super().__init__(f"guest operation {opcode} failed with Win32 error {code}")


class TransferError(RuntimeError):
    """A failed transfer keeps its stage and backup names for manual recovery."""


class NamedPipeTransport:
    """Windows overlapped I/O: a missing reply has a host-side deadline."""

    class Overlapped(ctypes.Structure):
        _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                    ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                    ("hEvent", wintypes.HANDLE)]

    def __init__(self, path: str = DEFAULT_PIPE, connect_timeout: float = 10):
        if os.name != "nt":
            raise OSError("the VirtualBox named-pipe transport requires a Windows host")
        if not path.startswith("\\\\.\\pipe\\"):
            raise ValueError("expected a local Windows named-pipe path")
        self.path = path
        self.handle = None
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        k = self.kernel
        k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                 ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                 wintypes.HANDLE]
        k.CreateFileW.restype = wintypes.HANDLE
        k.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
        k.WaitNamedPipeW.restype = wintypes.BOOL
        k.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL,
                                  wintypes.LPCWSTR]
        k.CreateEventW.restype = wintypes.HANDLE
        for name in ("ReadFile", "WriteFile"):
            fn = getattr(k, name)
            fn.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                           ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(self.Overlapped)]
            fn.restype = wintypes.BOOL
        k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        k.WaitForSingleObject.restype = wintypes.DWORD
        k.GetOverlappedResult.argtypes = [wintypes.HANDLE, ctypes.POINTER(self.Overlapped),
                                         ctypes.POINTER(wintypes.DWORD), wintypes.BOOL]
        k.GetOverlappedResult.restype = wintypes.BOOL
        k.CancelIoEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(self.Overlapped)]
        k.CancelIoEx.restype = wintypes.BOOL
        k.CloseHandle.argtypes = [wintypes.HANDLE]
        k.CloseHandle.restype = wintypes.BOOL
        deadline = time.monotonic() + connect_timeout
        invalid = ctypes.c_void_p(-1).value
        while True:
            handle = k.CreateFileW(path, 0xC0000000, 0, None, 3, 0x40000000, None)
            if handle != invalid:
                self.handle = handle
                break
            error = ctypes.get_last_error()
            if error not in (2, 231) or time.monotonic() >= deadline:
                raise OSError(error, f"cannot open {path}")
            k.WaitNamedPipeW(path, 200)
            time.sleep(0.05)

    def _io(self, data: bytes | None, size: int, deadline: float) -> bytes | int:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("remote pipe deadline expired")
        event = self.kernel.CreateEventW(None, True, False, None)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        operation = self.Overlapped()
        operation.hEvent = event
        buffer = ctypes.create_string_buffer(data, size) if data is not None else ctypes.create_string_buffer(size)
        count = wintypes.DWORD()
        fn = self.kernel.WriteFile if data is not None else self.kernel.ReadFile
        try:
            completed = fn(self.handle, buffer, size, ctypes.byref(count), ctypes.byref(operation))
            if not completed:
                error = ctypes.get_last_error()
                if error != 997:  # ERROR_IO_PENDING
                    raise OSError(error, "remote pipe I/O failed")
                wait = self.kernel.WaitForSingleObject(event, max(1, min(int(remaining * 1000), 0xFFFFFFFE)))
                if wait != 0:
                    # Reap cancellation before freeing OVERLAPPED/buffer storage.
                    self.kernel.CancelIoEx(self.handle, ctypes.byref(operation))
                    self.kernel.GetOverlappedResult(self.handle, ctypes.byref(operation), ctypes.byref(count), True)
                    if wait == 258:
                        raise TimeoutError("remote pipe response timed out; request was not retried")
                    raise OSError("remote pipe wait failed")
                if not self.kernel.GetOverlappedResult(self.handle, ctypes.byref(operation), ctypes.byref(count), False):
                    raise ctypes.WinError(ctypes.get_last_error())
            if not count.value:
                raise EOFError("remote pipe closed")
            return count.value if data is not None else buffer.raw[:count.value]
        finally:
            self.kernel.CloseHandle(event)

    def write_all(self, data: bytes, deadline: float) -> None:
        offset = 0
        while offset < len(data):
            offset += self._io(data[offset:], len(data) - offset, deadline)

    def read_exact(self, count: int, deadline: float) -> bytes:
        parts = []
        while count:
            block = self._io(None, count, deadline)
            parts.append(block)
            count -= len(block)
        return b"".join(parts)

    def close(self) -> None:
        if self.handle is not None:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Client:
    def __init__(self, transport, encoding: str = "cp949", io_timeout: float = 30):
        self.transport = transport
        self.encoding = encoding
        self.io_timeout = io_timeout
        self.request_id = 0

    def exchange(self, opcode: int, payload: bytes = b"", timeout: float | None = None) -> bytes:
        if len(payload) > MAX_PAYLOAD:
            raise ValueError("request payload exceeds protocol limit")
        self.request_id = self.request_id % 0xFFFFFFFF + 1
        deadline = time.monotonic() + (self.io_timeout if timeout is None else timeout)
        self.transport.write_all(HEADER.pack(MAGIC, VERSION, opcode, self.request_id, len(payload)) + payload, deadline)
        magic, version, reply_op, request_id, size = HEADER.unpack(self.transport.read_exact(HEADER.size, deadline))
        if (magic, version, reply_op, request_id) != (MAGIC, VERSION, opcode | REPLY, self.request_id):
            raise ProtocolError("reply header does not match the request")
        if not 4 <= size <= MAX_PAYLOAD:
            raise ProtocolError("invalid reply payload length")
        reply = self.transport.read_exact(size, deadline)
        error, = struct.unpack_from("<I", reply)
        if error:
            raise RemoteError(opcode, error)
        return reply[4:]

    def _string(self, value: str, limit: int, allow_empty: bool = False) -> bytes:
        data = value.encode(self.encoding, errors="strict")
        if b"\0" in data or len(data) > limit or (not allow_empty and not data):
            raise ValueError("invalid guest string length or embedded NUL")
        return data

    def _path(self, value: str) -> bytes:
        if not re.match(r"^[A-Za-z]:\\", value):
            raise ValueError("guest file paths must be absolute drive paths")
        if any(part in (".", "..") for part in value[3:].split("\\")) or "/" in value:
            raise ValueError("guest paths must not contain relative components")
        return self._string(value, 259)

    def ping(self) -> str:
        return self.exchange(1).decode("ascii", errors="strict")

    def execute(self, command: str, cwd: str = "", timeout_ms: int = 30000) -> dict:
        if not 1 <= timeout_ms <= 300000:
            raise ValueError("guest execution timeout must be 1..300000 ms")
        directory = self._path(cwd) if cwd else b""
        cmd = self._string(command, 4095)
        payload = struct.pack("<III", timeout_ms, len(directory), len(cmd)) + directory + cmd
        reply = self.exchange(2, payload, timeout_ms / 1000 + self.io_timeout)
        if len(reply) < 12:
            raise ProtocolError("short EXEC response")
        exit_code, timed_out, truncated = struct.unpack_from("<III", reply)
        if timed_out not in (0, 1) or truncated not in (0, 1) or len(reply) - 12 > CHUNK:
            raise ProtocolError("invalid EXEC metadata")
        output = reply[12:]
        return {"command": command, "cwd": cwd, "timeout_ms": timeout_ms,
                "exit_code": exit_code, "timed_out": bool(timed_out),
                "output_truncated": bool(truncated), "output_bytes": len(output),
                "output_sha256": hashlib.sha256(output).hexdigest(),
                "output": output.decode(self.encoding, errors="replace")}

    def read_chunks(self, path: str):
        encoded = self._path(path)
        offset, total = 0, None
        while total is None or offset < total:
            reply = self.exchange(3, struct.pack("<II", offset, CHUNK) + encoded)
            if len(reply) < 4:
                raise ProtocolError("short GET response")
            size, = struct.unpack_from("<I", reply)
            block = reply[4:]
            if total is None:
                total = size
            if size != total or len(block) > CHUNK or offset + len(block) > total:
                raise ProtocolError("GET size changed or chunk is out of range")
            if not block and offset < total:
                raise ProtocolError("GET returned premature EOF")
            yield block
            offset += len(block)

    def remote_hash(self, path: str) -> tuple[int, str]:
        result, size = hashlib.sha256(), 0
        for block in self.read_chunks(path):
            size += len(block)
            result.update(block)
        return size, result.hexdigest()

    def _put_stage(self, source: Path, path: str) -> dict:
        if source.stat().st_size > 0xFFFFFFFF:
            raise ValueError("file exceeds protocol v1 size range")
        encoded = self._path(path)
        offset, digest = 0, hashlib.sha256()
        with source.open("rb") as stream:
            while True:
                block = stream.read(CHUNK)
                if offset and not block:
                    break
                if offset + len(block) > 0xFFFFFFFF:
                    raise ValueError("file exceeds protocol v1 size range")
                payload = struct.pack("<III", offset, 2 if offset == 0 else 0, len(encoded)) + encoded + block
                reply = self.exchange(4, payload)
                if len(reply) != 4 or struct.unpack("<I", reply)[0] != len(block):
                    raise ProtocolError("PUT acknowledgement length mismatch")
                digest.update(block)
                offset += len(block)
                if not block:
                    break
        result = {"source": str(source), "guest_path": path, "bytes": offset,
                  "sha256": digest.hexdigest(), "verified": False}
        if self.remote_hash(path) != (offset, digest.hexdigest()):
            raise ProtocolError("uploaded guest file does not match local SHA-256")
        result["verified"] = True
        return result

    def put_file(self, source: Path, path: str) -> dict:
        """Verify a new sibling file, then promote it with a retained old backup.

        No mutating request is retried after an uncertain response. Win98 lacks
        an atomic replace primitive: PROMOTE rolls back ordinary rename errors,
        but a crash between renames requires recovery using the recorded names.
        """
        self._path(path)
        parent, name = ntpath.split(path)
        if not name or name.endswith((".", " ")):
            raise ValueError("guest destination must name a file")
        token = uuid.uuid4().hex[:8].upper()
        stage = ntpath.join(parent, token + ".TMP")
        backup = ntpath.join(parent, token + ".BAK")
        for value in (stage, backup):
            self._path(value)
        if len({path.casefold(), stage.casefold(), backup.casefold()}) != 3:
            raise ValueError("generated transfer paths collided")
        try:
            result = self._put_stage(source, stage)
            paths = [self._path(value) for value in (stage, path, backup)]
            reply = self.exchange(5, struct.pack("<III", *(len(value) for value in paths)) + b"".join(paths))
            if reply:
                raise ProtocolError("unexpected PROMOTE response data")
            if self.remote_hash(path) != (result["bytes"], result["sha256"]):
                raise ProtocolError("promoted guest file does not match local SHA-256")
        except Exception as error:
            raise TransferError(f"transfer failed: {error}; do not retry blindly; "
                                f"destination={path}, stage={stage}, backup={backup}") from error
        result.update({"guest_path": path, "stage_path": stage, "backup_path": backup,
                       "backup_policy": "retained if destination previously existed", "promoted": True})
        return result

    def get_file(self, path: str, destination: Path) -> dict:
        temporary = destination.with_name(f".{destination.name}.{uuid.uuid4().hex}.partial")
        digest, size = hashlib.sha256(), 0
        try:
            with temporary.open("xb") as stream:
                for block in self.read_chunks(path):
                    stream.write(block)
                    digest.update(block)
                    size += len(block)
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
        return {"guest_path": path, "destination": str(destination),
                "bytes": size, "sha256": digest.hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pipe", default=DEFAULT_PIPE)
    parser.add_argument("--encoding", default="cp949")
    parser.add_argument("--json", type=Path, help="also save the result as UTF-8 JSON")
    commands = parser.add_subparsers(dest="operation", required=True)
    commands.add_parser("ping")
    execute = commands.add_parser("exec")
    execute.add_argument("command")
    execute.add_argument("--cwd", default="")
    execute.add_argument("--timeout-ms", type=int, default=30000)
    upload = commands.add_parser("put")
    upload.add_argument("source", type=Path)
    upload.add_argument("guest_path")
    download = commands.add_parser("get")
    download.add_argument("guest_path")
    download.add_argument("destination", type=Path)
    args = parser.parse_args()
    with NamedPipeTransport(args.pipe) as transport:
        client = Client(transport, args.encoding)
        identity = client.ping()
        if args.operation == "ping":
            result = {"guest": identity}
        elif args.operation == "exec":
            result = client.execute(args.command, args.cwd, args.timeout_ms)
        elif args.operation == "put":
            result = client.put_file(args.source, args.guest_path)
        else:
            result = client.get_file(args.guest_path, args.destination)
        result.update({"guest": identity, "transport": "VirtualBox COM1 local named pipe", "pipe": args.pipe})
    rendered = json.dumps(result, ensure_ascii=True, indent=2)
    if args.json:
        args.json.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    if args.operation == "exec":
        return 1 if result["timed_out"] else min(result["exit_code"], 255)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
