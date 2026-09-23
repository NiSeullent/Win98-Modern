# Win98 remote lab

The visible `m98agent.exe` accepts commands and file transfers over the test
VM's COM1 connection to a local host named pipe. No guest network is enabled.
The Windows 98 OEM native export manifest is the import gate for guest tools.

## Build and bootstrap

From the repository root on the development Windows host:

```powershell
.\remote\build-guest.ps1
.\remote\build-fixtures.ps1
.\remote\build-app-probe.ps1
python remote/make_bootstrap_iso.py
.\remote\configure-vm.ps1
```

Configure serial only while the disposable test VM is off. The preserved
`Win98Modern-Base` is refused. Mount `vm/remote-bootstrap.iso` in its IDE CD
drive and start with `vm/run.ps1`, which checks the current hardware acceleration
log. The CD builder includes only the agent and a DOS CRLF batch file, checking
both ISO9660 and Joliet payloads. Run `D:\LAB\BOOTLAB.BAT` in the guest. It copies
the agent to `C:\M98LAB` and leaves its console visible.

If opening COM1 returns error 2, the virtual UART can exist without a Windows
device registration. In the Korean Win98 guest the manual hardware wizard can
be opened with `rundll32.exe sysdm.cpl,InstallDevice_Rundll`. Select Ports,
Communications Port, 03F8–03FF and IRQ 4; provide the original OEM installation
CD when it requests SERIALUI.DLL. Preserve a snapshot before device experiments.
The first post-install reboot in this lab hit a VxD exception; see the validation
status below before treating this bootstrap procedure as complete.

`remote/console.py` supplies paced bootstrap keys and screenshots without guest
additions. `run` opens the Run dialog and submits an ASCII command; `type`
submits text to the already focused console. Inspect the screen before typing.
For commands with significant quoting, `run-file`/`type-file` read one exact ASCII
line from a local file. This helper does not detect successful command execution.

## Host controller and tester

```powershell
python remote/controller.py ping
python remote/controller.py exec 'C:\M98LAB\remote_fixture.exe fail'
python remote/tester.py remote/suites/transport.json
python remote/tester.py remote/suites/api-direct.json
```

`transport.json` tests stdout/stderr, exit code 7, bounded child termination and
output truncation. `api-direct.json` transfers five DLL/test pairs and runs the
direct API checks. These are direct calls, not static KernelEx import validation.
The tester records the VM's current acceleration evidence, guest OS identity,
agent SHA-256, uploaded file hashes and individual command results in ignored
`build/remote` JSON files. Any broken stream stops the suite. Console EXEC is
bounded and terminates the immediate child on timeout; it is not a persistent
GUI launcher. See [GUI diagnostics](GUI_PROBE.md) for the window observer.

Uploads use a fresh sibling `.TMP`, SHA-256 readback, PROMOTE, then final readback.
An existing target moves to a retained `.BAK`. Ordinary rename failures attempt
rollback. This two-rename sequence is not atomic across power loss; an uncertain
reply requires inspection of the reported destination, stage and backup paths.
Never retry a mutating request automatically. The agent's partial-frame deadline
is 30 seconds; restart it visibly after a broken frame. Details: [protocol](PROTOCOL.md).

## Current evidence

Host protocol tests: `python -B -m unittest discover -s remote/tests -v` passes
9 cases, including real Windows named-pipe partial reads and deadline cancellation,
and simulated interrupted/corrupt transfers that preserve the destination.
These host tests do not establish Windows 98 serial behavior.

The guest agent builds as PE32 console 4.10 with 30 OEM-native KERNEL32 imports.
On 2026-09-23 COM1 registration completed using the OEM CD, but the next boot
raised VxD 0D and then 0E exceptions. The test clone was snapshotted as
`before-com-recovery-20260923`. Patcher9x v0.9.91 found VMM32 already patched for
TLB and made no change. Hardware/driver isolation is in progress. No successful
guest PING, file transfer, or remote test suite is claimed yet.
