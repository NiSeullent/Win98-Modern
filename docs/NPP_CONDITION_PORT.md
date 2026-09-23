# SRW condition-variable API port

Notepad++ 8.9.8 imports `KERNEL32.DLL!SleepConditionVariableSRW` and
`WakeAllConditionVariable`. This port also supplies the corresponding
`InitializeConditionVariable` and `WakeConditionVariable` APIs so a caller can
initialize a variable and wake either one or all waiters. These four names are
in the KernelEx `KERNEL32.DLL` table; this work does not establish that
Notepad++ starts or runs correctly.

## ABI and source review

- [Microsoft's SRW sleep contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepconditionvariablesrw) specifies `BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE, PSRWLOCK, DWORD, ULONG)`. The caller enters with the SRW lock held; the function releases and reacquires the same mode before returning, including after `ERROR_TIMEOUT`. `CONDITION_VARIABLE_LOCKMODE_SHARED` selects shared mode; otherwise the lock is treated as exclusive. A zero timeout returns promptly, and an infinite timeout does not expire.
- [Initialize](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializeconditionvariable), [wake one](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakeconditionvariable), and [wake all](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakeallconditionvariable) each use a one-pointer condition-variable object and a `VOID WINAPI` signature. A zero-initialized object is valid. Waking a variable with no waiters does not store a future signal. The object is process-local and must not be copied while in use.
- Pinned [Wine `dlls/ntdll/sync.c` lines 701–821](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/ntdll/sync.c#L701-L821) uses an address-wait sequence and reacquires the original SRW mode after the wait. Pinned [ReactOS `sdk/lib/rtl/condvar.c` lines 259–523](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/sdk/lib/rtl/condvar.c#L259-L523) registers a waiter, uses a keyed event, resolves a wake/timeout removal race, and then reacquires the lock. Neither upstream implementation can be linked directly to Win98: its NT wait mechanism is absent. Both files were analyzed; no upstream source was copied.

## Win98 implementation

`src/m98wrap.c` maintains a process-local FIFO queue of stack-owned waiters
under one native Win98 critical section. Each sleeping thread owns one
auto-reset event made with the original Win98 `CreateEventA`. A waiter is
enqueued before its caller-held SRW lock is released. The queue lock covers
both operations, so a concurrent wake either sees this waiter or occurs
before it begins sleeping. A wake signals only a queued waiter through native
`SetEvent`; `WakeConditionVariable` signals the first matching waiter and
`WakeAllConditionVariable` signals every currently queued waiter for that
object. Different condition-variable addresses do not share wakeups.

After `WaitForSingleObject`, the sleeping thread removes itself if the wake
did not already remove it, closes its private event, and reacquires its SRW
lock in the original mode. The wake/timeout decision is resolved under the
queue lock. A timeout returns `FALSE` with `ERROR_TIMEOUT` after reacquisition;
event creation or wait failure returns `FALSE` with the native error. If event
creation fails, the incoming SRW lock remains held. Wakes without waiters
have no retained credit. The SRW lock's existing pointer-sized atomic state
and writer preference remain unchanged.

The queue is linear in the number of current waiters and allocates a Win98
kernel event handle per waiter. It supports process-local SRW waits, including
shared and exclusive mode. `SleepConditionVariableCS` is a separate API and is
not included in this patch. Native Windows condition-variable ordering is not
guaranteed; this port selects a FIFO waiter for wake-one but the eventual
reacquisition order is still set by the SRW lock scheduler. Calling without
the indicated lock, modifying an active condition variable, or copying it
while waiters exist is undefined by the Windows contract and is not validated.

## Verification and guest handoff

Run `tools/build-condition.ps1`. The isolated build writes these PE32 i386
subsystem 4.10 files to `build/condition/`:

| File | Purpose | SHA-256 (2026-09-24 host build) |
| --- | --- | --- |
| `m98wrap.dll` | 52-entry KernelEx KERNEL32 table | `aef5d5af6c329b40c9c749f30dd0993b2cda96710923d30e5b0dc05cb748752d` |
| `condition_smoke.exe` | Direct API-table threaded behavior | `bace9a8ec6b152bca69e16d3464d524ad31b11ed1863235213389d451c1851f3` |
| `condition_import_probe.exe` | Static imports of all four names plus SRW lock entry points | `cc0d7379fb8f55d84813a2d38cbceefbfb699ac7e7230ff03b8caf08b26b00eb` |
| `smoke.exe` | Whole 52-entry table sample | `1e575a7075cba340ddce0f05546daf712522d23d548640ce855e664c8f73192c` |

The host build passed the PE/import gate, which checks the DLL and both
executables against the pinned original Win98 KERNEL32 export manifest.
The direct host test passed wake-one versus wake-all, two exclusive waiters,
shared wait/reacquisition, unrelated-variable isolation, zero and finite
timeouts, no stored wake, and blocking until the original lock can be
reacquired. Twelve further direct host runs passed. The static-import probe
also passed **on the host**, where imports resolve to its modern KERNEL32.

The directly installed Korean Windows 98 SE guest received the exact DLL as
`C:\\WINDOWS\\KernelEx\\M98WRP11.DLL` (SHA-256
`aef5d5af6c329b40c9c749f30dd0993b2cda96710923d30e5b0dc05cb748752d`).
The existing `CORE.INI` was read back, then its sole `DCFG1` provider token
was changed from `m98wrp10` to `m98wrp11` without replacing the other bytes;
the resulting file SHA-256 was
`aa773d950ffc8777274c30ff3600c551d428d228f49b5223a82667559224790e`.
After a normal guest shutdown and cold boot under the VirtualBox WHPX/NEM
hardware acceleration backend, the guest tests returned:

| Probe | Observed guest result |
| --- | --- |
| Direct API-table threaded test (`CONDS.EXE`) | Exit 0, `PASS: condition variable wake-one/wake-all, shared/exclusive and timeout` |
| Static `KERNEL32` import (`CONI.EXE`) | Exit 0, `PASS: static KERNEL32 condition-variable imports` |
| Full table sample (`SMK52.EXE`) | Exit 0, `PASS: 52-entry KernelEx table and sampled API behavior` |

The pinned `Notepad++ 8.9.8` executable was launched again through the guest
app probe. It still failed to load (Win32 error 31), but the new loader dialog
was `KERNEL32.DLL!CloseThreadpoolWork`, recorded in
`vm/npp-after-condition.png`. This proves the two required condition-variable
imports were passed by the loader in this guest. It does **not** prove their
feature-path behavior inside the app or that Notepad++ starts.
