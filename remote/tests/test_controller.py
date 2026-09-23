"""Host-only protocol and real Windows pipe tests; not Win98 guest evidence."""

import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import threading
import time
import unittest
import uuid

spec = importlib.util.spec_from_file_location("m98_controller", Path(__file__).resolve().parents[1] / "controller.py")
controller = importlib.util.module_from_spec(spec)
spec.loader.exec_module(controller)


class AgentDouble:
    def __init__(self):
        self.files = {}
        self.input = b""
        self.override = None
        self.fail_put_offset = None
        self.corrupt_get = False
        self.fail_promote = False

    def write_all(self, frame, deadline):
        magic, version, op, request, size = controller.HEADER.unpack_from(frame)
        assert magic == controller.MAGIC and version == 1 and len(frame) == 20 + size
        data = frame[20:]
        status, reply = 0, b""
        if op == 1:
            reply = b"HOST TEST DOUBLE ONLY"
        elif op == 2:
            timeout, cwd_len, cmd_len = struct.unpack_from("<III", data)
            assert len(data) == 12 + cwd_len + cmd_len
            reply = struct.pack("<III", 7, 0, 0) + b"stdout\r\nstderr\r\n"
        elif op == 4:
            offset, flags, path_len = struct.unpack_from("<III", data)
            path, block = data[12:12 + path_len], data[12 + path_len:]
            if self.fail_put_offset == offset:
                raise TimeoutError("injected partial transfer interruption")
            if flags == 2 and path in self.files:
                status = 80
            elif flags in (1, 2):
                self.files[path] = b""
            if status:
                pass
            elif path not in self.files or len(self.files[path]) != offset:
                status = 87
            else:
                self.files[path] += block
                reply = struct.pack("<I", len(block))
        elif op == 3:
            offset, count = struct.unpack_from("<II", data)
            path = data[8:]
            if path not in self.files:
                status = 2
            else:
                reply = struct.pack("<I", len(self.files[path])) + self.files[path][offset:offset + count]
                if self.corrupt_get and len(reply) > 4:
                    reply = reply[:-1] + bytes([reply[-1] ^ 1])
        elif op == 5:
            a, b, c = struct.unpack_from("<III", data)
            source = data[12:12+a]
            dest = data[12+a:12+a+b]
            backup = data[12+a+b:]
            assert len(backup) == c
            if self.fail_promote:
                status = 32
            elif source not in self.files or backup in self.files:
                status = 87
            else:
                if dest in self.files:
                    self.files[backup] = self.files.pop(dest)
                self.files[dest] = self.files.pop(source)
        payload = struct.pack("<I", status) + reply
        self.input = controller.HEADER.pack(controller.MAGIC, 1, op | controller.REPLY, request, len(payload)) + payload
        if self.override:
            self.input = self.override(self.input)

    def read_exact(self, size, deadline):
        if len(self.input) < size:
            raise EOFError("short fake response")
        result, self.input = self.input[:size], self.input[size:]
        return result


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.transport = AgentDouble()
        self.client = controller.Client(self.transport)

    def test_exec_preserves_exit_and_output(self):
        result = self.client.execute(r"C:\M98LAB\probe.exe", r"C:\M98LAB", 1000)
        self.assertEqual(result["exit_code"], 7)
        self.assertEqual(result["output"], "stdout\r\nstderr\r\n")
        self.assertFalse(result["timed_out"])

    def test_transfer_empty_and_multichunk_binary_with_readback(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.bin"
            destination = Path(directory) / "output.bin"
            for data in (b"", bytes(range(256)) * 600):
                source.write_bytes(data)
                result = self.client.put_file(source, r"C:\M98LAB\probe.bin")
                self.assertTrue(result["verified"])
                self.assertTrue(result["promoted"])
                self.assertEqual(result["sha256"], hashlib.sha256(data).hexdigest())
                self.client.get_file(r"C:\M98LAB\probe.bin", destination)
                self.assertEqual(destination.read_bytes(), data)

    def test_replacement_keeps_backup_and_removes_stage(self):
        path = r"C:\M98LAB\probe.dll"
        self.transport.files[path.encode()] = b"old working DLL"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "new.dll"
            source.write_bytes(b"new verified DLL")
            result = self.client.put_file(source, path)
        self.assertEqual(self.transport.files[path.encode()], b"new verified DLL")
        self.assertEqual(self.transport.files[result["backup_path"].encode()], b"old working DLL")
        self.assertNotIn(result["stage_path"].encode(), self.transport.files)

    def test_interruption_corruption_and_promotion_failure_preserve_destination(self):
        path = r"C:\M98LAB\probe.dll"
        for failure in ("interrupt", "corrupt", "promote"):
            self.setUp()
            self.transport.files[path.encode()] = b"old working DLL"
            self.transport.fail_put_offset = controller.CHUNK if failure == "interrupt" else None
            self.transport.corrupt_get = failure == "corrupt"
            self.transport.fail_promote = failure == "promote"
            with tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / "new.dll"
                source.write_bytes(bytes(range(256)) * 300)
                with self.assertRaises(controller.TransferError) as error:
                    self.client.put_file(source, path)
            self.assertIn("stage=", str(error.exception))
            self.assertIn("backup=", str(error.exception))
            self.assertEqual(self.transport.files[path.encode()], b"old working DLL")

    def test_rejects_mismatched_reply_and_oversize(self):
        for offset, value in ((0, b"BAD!"), (4, struct.pack("<I", 2)),
                              (12, struct.pack("<I", 0)),
                              (16, struct.pack("<I", controller.MAX_PAYLOAD + 1))):
            self.transport.override = lambda frame, o=offset, v=value: frame[:o] + v + frame[o + 4:]
            with self.assertRaises(controller.ProtocolError):
                self.client.ping()

    def test_remote_file_error_is_not_success(self):
        with self.assertRaises(controller.RemoteError) as error:
            self.client.remote_hash(r"C:\M98LAB\missing.bin")
        self.assertEqual(error.exception.code, 2)

    def test_paths_nuls_and_execution_limits(self):
        for path in ("relative.bin", r"\\server\share\test", "C:\\x\0bad"):
            with self.assertRaises(ValueError):
                self.client.remote_hash(path)
        for timeout in (0, 300001):
            with self.assertRaises(ValueError):
                self.client.execute("probe.exe", timeout_ms=timeout)


@unittest.skipUnless(os.name == "nt", "requires a Windows host")
class NamedPipeTests(unittest.TestCase):
    def setUp(self):
        import ctypes
        from ctypes import wintypes
        self.ctypes, self.wintypes = ctypes, wintypes
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateNamedPipeW.argtypes = [wintypes.LPCWSTR] + [wintypes.DWORD] * 6 + [ctypes.c_void_p]
        self.kernel.CreateNamedPipeW.restype = wintypes.HANDLE
        self.kernel.ConnectNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
        self.kernel.ConnectNamedPipe.restype = wintypes.BOOL
        for name in ("ReadFile", "WriteFile"):
            fn = getattr(self.kernel, name)
            fn.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
            fn.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.path = r"\\.\pipe\M98-HostTest-" + uuid.uuid4().hex
        self.handle = self.kernel.CreateNamedPipeW(self.path, 3, 0, 1, 65536, 65536, 0, None)
        self.assertNotEqual(self.handle, ctypes.c_void_p(-1).value)

    def tearDown(self):
        self.kernel.CloseHandle(self.handle)

    def serve(self, slow=False):
        self.kernel.ConnectNamedPipe(self.handle, None)
        count = self.wintypes.DWORD()
        if slow:
            time.sleep(0.2)
            return
        block = self.ctypes.create_string_buffer(5)
        self.kernel.ReadFile(self.handle, block, 5, self.ctypes.byref(count), None)
        if block.raw[:count.value] == b"hello":
            for data in (b"wo", b"rld"):
                self.kernel.WriteFile(self.handle, data, len(data), self.ctypes.byref(count), None)
                time.sleep(0.01)

    def test_real_pipe_partial_read(self):
        worker = threading.Thread(target=self.serve)
        worker.start()
        try:
            with controller.NamedPipeTransport(self.path) as pipe:
                pipe.write_all(b"hello", time.monotonic() + 1)
                self.assertEqual(pipe.read_exact(5, time.monotonic() + 1), b"world")
        finally:
            worker.join(2)
        self.assertFalse(worker.is_alive())

    def test_real_pipe_deadline_and_cancel(self):
        worker = threading.Thread(target=self.serve, kwargs={"slow": True})
        worker.start()
        try:
            with controller.NamedPipeTransport(self.path) as pipe:
                with self.assertRaises(TimeoutError):
                    pipe.read_exact(1, time.monotonic() + 0.03)
        finally:
            worker.join(2)
        self.assertFalse(worker.is_alive())


if __name__ == "__main__":
    unittest.main()
