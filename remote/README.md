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
The first post-install reboot in this lab hit a VxD exception with a 16550A
UART. The recovered test clone subsequently booted and passed the suites with
the VM set to a 16450 UART. Keep that setting for the current evidence.

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
python remote/controller.py stop
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
10 cases, including real Windows named-pipe partial reads and deadline cancellation,
and simulated interrupted/corrupt transfers that preserve the destination.
These host tests alone do not establish Windows 98 serial behavior.

The guest agent builds as PE32 console 4.10 with 30 OEM-native KERNEL32 imports.
On 2026-09-23 COM1 registration completed using the OEM CD, but the next boot
raised VxD 0D and then 0E exceptions. The test clone was snapshotted as
`before-com-recovery-20260923`. Patcher9x v0.9.91 found VMM32 already patched for
TLB and made no change. UART-off recovery reached safe mode, followed by a clean
restart to the normal desktop. A 16450 UART then booted the normal GUI and
connected to the agent. The exact cause of the earlier 16550A boot exception
remains undetermined.

With that 16450 setup, `build/remote/transport-16450-first.json` recorded 3/3
guest transport cases passing. `build/remote/api-direct-16450-first.json`
recorded 5/5 direct API cases passing for KERNEL32, BCRYPT, DBGHELP, DWMAPI and
SHELL32 after SHA-256 verified file transfers. These ignored local evidence
files identify the guest as Windows 4.10 and record current hardware-acceleration
evidence. The newer agent uploaded to `C:\M98LAB\M98AG2.EXE` also acknowledged
STOP in the guest and its console exited cleanly. This verifies the lab channel
and the direct cases only; it does not establish application startup.

## Lifecycle family regression bundle

After installing the matching provider and the six KERNEL32 groups in
`porting/runtime-routes.json`, cold boot, start `C:\M98LAB\M98AG2.EXE`, and
wait for the visible agent-ready line. Then run:

```powershell
python remote/tester.py remote/suites/api-lifecycle.json --output build/guest/lifecycle-results.json
```

The suite verifies the selected `agent_guest_path` against the local agent
binary, uploads explicit test files with hash checks, and runs ten FLS, CRT,
provider-table, InitOnce, threadpool, SList and NLS checks. The file hash proves
the selected on-disk agent image; launching that image is a separate bootstrap
step. The default path for older suites remains `M98AGENT.EXE`. The suite does
not edit CORE.INI or perform provider upgrades. It records the current boot's
hardware acceleration evidence.

## Preparing separate provider routes

`porting/runtime-routes.json` version 2 records the target DLL and provider table
index for each reviewed family. `patch_core_family.py --family all` keeps the
existing KERNEL32 union; use `--target-dll USER32.DLL` for a reviewed USER32 union.
An individual family chooses its declared DLL automatically. A conflicting
`--target-dll` is rejected, as are mixed table indices, malformed/conflicting
routes, duplicate profiles and missing profiles. Unrelated CORE bytes survive.

The provider must already occur once in DCFG1 `contents`, unless
`--register-provider` explicitly appends a new library. This prepares a new
output file only. Copy and verify the matching versioned DLL before installing
the prepared CORE.INI, retain the original bytes for recovery, then cold boot
and validate static imports. The helper does not prove that a DLL implements
the selected API contracts; promotion still requires review and guest evidence.
