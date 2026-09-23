# Windows 98 threadpool work bridge

## Why this port exists

The pinned Notepad++ 8.9.8 x86 EXE imports `CloseThreadpoolWork`,
`SubmitThreadpoolWork`, `CreateThreadpoolWork`, and
`FreeLibraryWhenCallbackReturns` directly from `KERNEL32.DLL` (see
`docs/NPP_IMPORT_TRIAGE_8_9_8.md`). After the earlier import bridges, a direct
Windows 98 launch observed the `CloseThreadpoolWork` loader failure. This
module adds those four names and `WaitForThreadpoolWorkCallbacks`, which makes
the work-object lifetime and cancellation behavior testable.

The original Korean Windows 98 SE OEM `KERNEL32.DLL` export manifest contains
none of these five names, but does contain the thread, semaphore, event,
critical-section, heap, and module APIs this bridge imports. The module is
intended to be linked into `M98WRAP.DLL` and listed in its KernelEx API table.

## Contract and source review

- Microsoft documents [CreateThreadpoolWork](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-createthreadpoolwork), [SubmitThreadpoolWork](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-submitthreadpoolwork), [WaitForThreadpoolWorkCallbacks](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-waitforthreadpoolworkcallbacks), [CloseThreadpoolWork](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-closethreadpoolwork), and [FreeLibraryWhenCallbackReturns](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-freelibrarywhencallbackreturns). Each submission means one asynchronous callback; waits may cancel callbacks that have not started; closing releases the caller's work-object ownership while outstanding callbacks keep it alive; DLL unload is deferred until callback completion.
- Pinned [Wine `ntdll/threadpool.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/ntdll/threadpool.c) was reviewed at `TpAllocWork`, `tp_object_submit`, `tp_object_cancel`, `tp_object_execute`, `TpReleaseWork`, `TpWaitForWork`, and `TpCallbackUnloadDllOnCompletion`. Pinned [Wine `kernelbase/thread.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/thread.c) supplies the Win32 work-object wrapper.
- Pinned [ReactOS `sdk/lib/rtl/threadpool.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/sdk/lib/rtl/threadpool.c) was reviewed at its matching work-object lifetime paths; [ReactOS `kernel32_vista/threadpool.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32_vista/threadpool.c) contains the `CreateThreadpoolWork` front end. No Wine or ReactOS code was copied.

The public Close API page does not explicitly state whether queued callbacks
run after Close, while an older Microsoft magazine article says queued callbacks
are canceled. Pinned Wine and ReactOS keep queued callbacks outstanding after
Close. A bounded test on the Windows host used a real one-worker Microsoft
thread pool, blocked its worker, queued another work item, closed that item,
then unblocked the worker: the queued callback still executed. This bridge
follows that observed behavior. Explicit `Wait(..., TRUE)` cancels queued work.

## Win98 implementation

`src/m98_threadpool.c` has one process-level four-worker pool. The first
`CreateThreadpoolWork` starts its workers using native Win98 `CreateThread`;
creation fails with a Win32 error if no worker can start. Every work object has
its callback, context, completion event, pending and running counts, and a
reference for caller ownership plus each submission. `SubmitThreadpoolWork`
only increments a count and queues the object; it needs no per-submission heap
allocation. Workers dequeue one invocation and requeue the object when more
invocations remain, so callbacks for one work object can run in parallel.

`CloseThreadpoolWork` drops the caller's reference without waiting. Pending
submissions and running callbacks hold the object until the last callback has
completed. `WaitForThreadpoolWorkCallbacks(work, TRUE)` removes queued
invocations and waits only for already running callbacks. The callback gets a
per-invocation instance; `FreeLibraryWhenCallbackReturns` records the first
module given on that callback thread and calls native Win98 `FreeLibrary`
after the callback and optional finalization callback return, before the work
object signals completion.

A null/default callback environment is supported. A default V1 or V3
environment with no custom pool, cleanup group, activation context, race DLL,
or nonnormal priority is accepted; a finalization callback and the `LongFunction`
hint are accepted. Environments requiring those unavailable services fail
explicitly with `ERROR_NOT_SUPPORTED` at creation. This implementation does not
offer arbitrary pool sizing, cleanup groups, worker prioritization, or
`DisassociateCurrentThreadFromCallback`. Waiting on the same work object from
its own callback can deadlock. The KernelEx API library is expected to remain
loaded for process lifetime; manual unload while worker threads exist is not
supported. These are material differences from the complete NT thread pool.

The provider now always supplies a writable local `DWORD` for native
`CreateThread`'s `lpThreadId` parameter. Pinned KernelEx
[`CreateThread_fix`](../third_party/KernelEx/apilibs/kexbases/Kernel32/thread.c)
(lines 116-131) replaces a NULL/invalid pointer with a writable dummy, but a
provider's native import can execute before that override is applied. The
module also preserves the original Win32 error across cleanup when
`CreateSemaphoreA`, `CreateEventA`, or `CreateThread` fails. If one of those
native calls fails without setting LastError, the bridge reports the generic
`ERROR_GEN_FAILURE` instead of inventing a resource-exhaustion cause.

## Verification

Run `./tools/build-threadpool.ps1` from the repository root. The isolated
host build passed:

1. PE32 i386, Windows 98 subsystem 4.10, no ASLR/NX/TLS/delay-import/CLR,
   and original-OEM native-import gates for the fixture and test EXEs. The
   8.3 marker DLL also has a native `GetTickCount` import, relocations, and
   a different preferred image base from the fixture DLL.
2. Direct API-table smoke: invalid callback and unsupported environment;
   default V1 and V3 environments with finalization callback;
   parallel execution of two callbacks; waiting for blocked callbacks;
   cancellation of queued callbacks with `Wait(..., TRUE)`; queued callbacks
   surviving Close; an active callback surviving Close; and deferred unloading
   of a real test DLL only after the callback returns.
3. Static host `KERNEL32.DLL` import probe for all five names.
4. Relocation regression probe: reserve the marker's preferred `0x64000000`
   address, load both fixture and marker, verify the marker moved, and call
   its `GetTickCount`-backed export. This passed on the host; a guest result
   remains separate.

The direct Windows 98 SE guest test used the rebuilt fixture, `TPMARK.DLL`,
and `threadpool_smoke.exe` together in `C:\M98LAB`. The remote shell reported
exit code **0** and:

`PASS: Win98 threadpool work lifecycle, parallelism, cancel, deferred unload`

This proves the isolated API-table implementation ran its focused tests in the
installed guest. The same functionality test, built with `M98_INTEGRATED`,
then loaded the 59-entry production `M98WRAP.DLL` from the application folder
and also exited 0 with the same PASS line. This establishes focused behavior
through the integrated API table, independently of static import routing.

Reproducible build SHA-256 values for the current isolated handoff are below.
The earlier direct guest PASS used fixture SHA
`d1a31f6f42fb72415b2075668aaafc7742841cc66d40c22198a5a02499943421`;
the current fixture includes the native `CreateThread` thread-ID fix and has
not yet been run in the guest.

| File | SHA-256 |
| --- | --- |
| `threadpool_fixture.dll` | `08809ddf51d0272bbcf36b9c3fe9be3cdc3980b9b85316aed3afb72688405085` |
| `TPMARK.DLL` | `38f7c0811872b91f2716a00a282d9445ef447ee06659c4b558bdccb86c5ea56e` |
| `threadpool_smoke.exe` | `f11cdf6d64ba2ed692923293c018c4b2a1bf11a01d1538a0260b380b4b0fa2c4` |
| `threadpool_integrated_probe.exe` | `27e5daec25b5797939c1b4214e974bc8d86c3421701816d4c0c292e1845ef973` |
| `threadpool_import_probe.exe` | `7f429700edcb319ddb5d921741a007103c0cd6bf124dc0c864faa6d6eec678` |

The first direct guest run loaded the fixture but failed to load the marker
DLL. That DLL had a long filename, shared the fixture's preferred `0x10000000`
image base, and lacked a relocation table. The rebuilt `TPMARK.DLL` has an
8.3 name, a distinct `0x64000000` preferred base, a native import, and a
relocation directory. With this build the direct guest test passed. The first
static KernelEx `KERNEL32.DLL` import test through versioned `M98WRP13.DLL`
returned `FAIL: static CreateThreadpoolWork import`, while the integrated
table test passed. The installed `CORE.INI` initially had no explicit routes
for these five names. After installing a versioned provider and 15 explicit
routes in three profiles, a normal guest restart still produced a static
`CreateThreadpoolWork` failure. The IAT was `0x100038A4`, matching the
provider's symbol address, but the return was NULL with
`GetLastError() == 0`. The old provider passed NULL as native
`CreateThread`'s output thread-ID pointer, and its failure cleanup could erase
the original error. The corrected module passes a writable pointer and
preserves native failure errors.

The corrected production `M98WRP15.DLL` then passed both the simple static
probe and the **complete lifecycle suite through ordinary KERNEL32 imports**
in the directly installed Windows 98 SE guest. The full suite includes
parallelism, cancellation, close while queued/active, and deferred DLL unload:

`PASS: static Win98 threadpool lifecycle, parallelism, cancel, deferred unload`

The remote shell reported exit code 0, no timeout and 79 output bytes.
Provider SHA-256 was
`6fdc7c23abf31986bdbdb9a28bd751ffe134efe0baceb7539bc95fccf51a576b`;
`TPFULL.EXE` (`threadpool_guest_import_smoke.exe`) SHA-256 was
`6c03fda354102ffbff0f732589461ebcb32e532d8d15d0718d6cbdffe2662810`;
output SHA-256 was
`8d211601f8525aac30b95958fd294b4825db7ad97e868fd67a3b07127db6b37a`.
This provider precedes the later generic-error fallback adjustment, so that
adjustment still requires the next integrated-provider guest run.

That next run also passed: `M98WRP16.DLL`, SHA-256
`e519192a39ca04f5bc2563a9fd42ac3d4d406209b4bd024ded481a3c3d9714b9`,
ran the same `TPFULL.EXE` after an accelerated cold boot and returned exit 0
with the same output hash. This includes the generic-error fallback change
in the installed provider, although these positive cases do not force that
native failure path. The exact receipt is in
`benchmarks/api-guest-evidence-v1.json`.

Notepad++ 8.9.8 moved past these imports and next reported missing
`KERNEL32.InitOnceBeginInitialize`; application startup is not yet successful.
The complete threadpool family also includes timers, waits, I/O and cleanup
groups that are not implemented by this five-API work-object module. The
catalogue keeps those siblings in the same family backlog.
