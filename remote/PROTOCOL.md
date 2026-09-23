# M98 remote test protocol, version 1

This is a host-to-guest lab channel over a dedicated VirtualBox COM1 named pipe.
It is intentionally local to the host, with no guest network listener. The guest
agent runs visibly until stopped. The host owns the VM and every command sent.

COM1 settings: 115200 baud, 8 data bits, no parity, one stop bit, no flow control.
All integers below are unsigned 32-bit little-endian. A frame has a 20-byte
header: four literal bytes `M98R`, version `1`, opcode, request ID, payload size.
The payload size must not exceed 131072. Requests are processed sequentially.
A reply repeats the request ID and uses request opcode OR `0x80000000`.
Every reply payload begins with a Win32 error code (zero means success).
On failure, only that four-byte error is required. Invalid framing terminates
the connection; never scan payload data for an accidental magic sequence.

Strings are raw bytes in the guest ANSI code page, without trailing NULs. No
embedded NUL is accepted. Host paths used for transfer must be absolute drive
paths, with a maximum of 259 bytes. Neither side assumes UTF-8 in the guest.

| Opcode | Request after header | Success reply after error code |
| --- | --- | --- |
| 1 PING | empty | ASCII identity, including protocol/build identity and guest OS version |
| 2 EXEC | timeout milliseconds, working-directory byte count, command byte count, directory bytes, command bytes | exit code, timed-out flag, output-truncated flag, combined stdout/stderr bytes |
| 3 GET | offset, requested count (1..65536), absolute path bytes | total file size, returned file bytes (at most requested count) |
| 4 PUT | offset, flags, path byte count, absolute path bytes, file bytes (at most 65536) | number of bytes written |
| 5 PROMOTE | source byte count, destination byte count, backup byte count, then the three path strings | empty |

EXEC permits 1..300000 milliseconds, at most 259 directory bytes (zero uses
the agent directory), and 1..4095 command bytes. CreateProcess receives a
mutable command string and inherits file-backed stdout/stderr handles; stdin
is the NUL device. The caller explicitly invokes COMMAND.COM when shell
syntax is needed. Capture at most 65536 output bytes and set truncated if more
were produced. On timeout terminate and reap the immediate child, report
timed-out=1, and return captured output. Descendant process termination is not
guaranteed by this first transport version and must not be inferred by tests.

PUT flag 1 means create/truncate and is valid only at offset zero. Flag 2
means CREATE_NEW, also only at offset zero: an existing path is rejected. Zero
flags opens an existing file, checks that its current size equals offset,
then appends the chunk. All other bits are invalid. Parent directories must
already exist. A zero-byte initial PUT creates an empty file. The host checks
the acknowledged count on every chunk; it does not retry mutating frames
automatically after a lost reply. GET may request offset equal to file size
and receive an empty chunk; an offset beyond the size is invalid. GET rejects
files exceeding the 32-bit size range. File handles close before each reply.

The controller uploads to a unique sibling `.TMP` with CREATE_NEW, reads it
back to verify SHA-256, then sends PROMOTE. PROMOTE requires three distinct
case-insensitive absolute paths in the same directory, a regular source file,
and an absent backup path. If the destination exists it is moved to the backup;
then source is moved to destination. An ordinary second-move failure triggers
rollback. A rollback failure returns ERROR_GEN_FAILURE and prints diagnostics.
Successful replacement retains the old backup for recovery. A power loss between
moves is not atomic: recovery may require the recorded stage/backup paths.
Unknown results are never retried automatically. The destination is read back
again after successful promotion. Backups are not automatically deleted.

The host uses overlapped named-pipe I/O with deadlines. It rejects wrong
version/opcode/request IDs, oversize frames, truncated responses, impossible
chunk lengths, and nonzero errors. Test logs record the actual transport,
guest identity, command, timeout, exit status, output, and file hashes where
available. A host emulation test is never recorded as Windows 98 evidence.

The guest waits indefinitely for the first byte of an idle request, then imposes
a wrap-safe 30-second deadline on the remaining frame. Driver reads and writes
have finite waits; an incomplete or invalid frame stops the agent visibly.
Restart the agent after a broken frame; there is no in-band resynchronization.
