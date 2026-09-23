# Windows 98 guest agent

Build on the development host with `powershell -ExecutionPolicy Bypass -File remote/build-guest.ps1`. The resulting `build/m98agent.exe` is a standalone PE32 console program with subsystem 4.10 and only native KERNEL32 imports. The build runs `remote/check_guest_pe.py` against the OEM ISO export manifest.

Copy the executable into the Windows 98 guest, configure its COM1 as the dedicated VirtualBox host pipe, and run `m98agent.exe` from a visible console. The agent opens `COM1`, requests 64 KiB receive and transmit queues, configures 115200 8N1 with no software or hardware flow control, then processes protocol version 1 requests sequentially. A queue request failure is printed as a warning because the driver may still provide usable queues. COM initialization failures print the Win32 error number. Protocol bytes go only over COM1. Bad frame headers, unknown opcodes, oversize frames, and serial I/O failure end the agent. Restart it for a new session.

`EXEC` runs a mutable command line in the requested directory, or in the agent's directory when the request omits it. Stdout and stderr share a temporary file; stdin is `NUL`. The agent waits up to 300 seconds, terminates and reaps the immediate child on timeout, and returns at most 65536 output bytes. A descendant may outlive the child. The host should quote executable paths and invoke `COMMAND.COM` explicitly for shell syntax.

`GET` and `PUT` accept absolute drive paths of at most 259 ANSI bytes. The host must create parent directories. `PUT` acknowledges only the bytes it actually writes; an append requires the current file size to equal the given offset. Each file handle is closed before its reply.

`PUT` flag 2 uses CREATE_NEW for a staging file. The controller verifies the
staged contents before `PROMOTE` moves an existing destination to a unique
backup and moves the stage into place. Failed second moves attempt rollback;
backups are retained after successful replacement. A power loss between these
two moves is not atomic. Follow the recorded paths to recover uncertain results.

Serial driver calls use finite waits. The first byte starts a 30-second request
frame deadline; waiting for a new frame while idle has no deadline. A broken
partial frame ends the agent visibly and requires a restart.

The PE check is static loader evidence. A successful COM1 exchange in the Windows 98 guest is needed before calling the transport guest-tested.
