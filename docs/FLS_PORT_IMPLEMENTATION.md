# Win98 FLS and fiber/thread lifecycle bridge

Status: independent `FLSFIX.DLL` implementation and direct API-table tests.
The four FLS functions are real bounded implementations, but the complete
Windows thread/fiber lifecycle is **not** routed through them yet. No full
Windows API or application compatibility percentage follows from this test.

## Source lineage and native boundary

This implementation reads, but does not copy, pinned upstream source bodies:

- [Wine `dlls/kernelbase/thread.c` at df15af3](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/thread.c#L1081-L1279): native fiber creation/conversion/switch/deletion and the public `Fls*` wrappers. Its `ConvertThreadToFiberEx` transfers the thread's FLS data to the new fiber and `SwitchToFiber` selects the fiber's FLS data.
- [Wine `dlls/ntdll/thread.c` at df15af3](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/ntdll/thread.c#L500-L725): process callback slots, per-fiber data list, index zero reservation, free and rundown. Wine invokes callbacks under its global FLS lock on some paths; this bridge invokes callbacks after detaching values and releasing its lock.
- [ReactOS `dll/win32/kernel32/client/fiber.c` at 9dc3ca8](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/client/fiber.c#L30-L509): per-fiber data rundown, conversion inheritance, delete integration and process bitmap. Its TEB/PEB fields are NT internals and are not reused on Win98.
- [KernelEx `core/main.cpp` at 31cdfc3](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/core/main.cpp#L183-L228): `PreDllMain` calls `DisableThreadLibraryCalls(instance)`. The core itself has no natural thread detach notification path for this bridge. [KernelEx `apilibs/kexbases/main.c`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/main.c#L198-L278) does see `DLL_THREAD_DETACH`, but it is a loader-lock path; user FLS callbacks must not be run there.

The files `src/m98_fls.c/.h` are new GPL-2.0-only project code. Wine is
LGPL-2.1-or-later; ReactOS `fiber.c` carries GPL-2.0-or-later and authorship
notices. Their implementation code, NT structures and comments were not
copied. The [Microsoft FLS contract](https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsalloc)
and [fiber overview](https://learn.microsoft.com/en-us/windows/win32/procthread/fibers)
define public behavior; direct original-Win98 and modern-host probes define
the tested platform behavior.

## Implemented bounded behavior

`m98_FlsAlloc`, `m98_FlsFree`, `m98_FlsGetValue`, and `m98_FlsSetValue` use one
process table of 128 slots, with indexes 1–127 allocatable. Each active slot
has a callback and generation. Each unconverted thread or registered native
fiber has its own values and generation stamps. A private native TLS index
holds only the current thread context; values are **not** TLS aliases.
`GetCurrentFiber` identifies the selected registered fiber. The directly
installed Win98 SE baseline reported raw fiber address zero before conversion,
and its converted pointer matched `GetCurrentFiber`. A modern host has a
different raw pre-conversion value, so the bridge dynamically calls the host's
`IsThreadAFiber` when available. An unknown fiber is rejected with
`ERROR_INVALID_HANDLE` on FLS get/set; an untracked fiber is never guessed or
accepted just because it has a nonzero address.

The bridge wraps native `ConvertThreadToFiber` so existing unconverted-thread
values move to its new fiber record. It wraps `CreateFiber` with a start thunk
that preserves the original native fiber parameter and therefore
`GetFiberData`. `DeleteFiber` clears a registered fiber's values and invokes
its non-NULL callbacks before native deletion, including deletion of the
current fiber. A completed fiber start routine also passes through that
cleanup. `SwitchToFiber` delegates to the native context switch; FLS selection
uses the actual current native fiber pointer. Explicit `CreateFiberEx` and
`ConvertThreadToFiberEx` entries accept only the representable zero-flag
subset. They fail with `ERROR_CALL_NOT_IMPLEMENTED` for unsupported flags or
distinct reserve/commit sizes rather than claiming full semantics.

`FlsFree` marks its index retiring, detaches one value at a time from every
live or dying record, calls the captured callback outside the global lock, and only
then makes the index reusable with a new generation. Same-index get/set/free
cannot succeed during retirement. `DeleteFiber` marks the record dying and
detaches each value immediately before calling its callback outside the lock.
Pending values remain visible to a reentrant `FlsFree` until they are detached;
there is no array of cached callbacks surviving slot free/reallocation.
Each record has an instance serial; a native fiber address reused after
deletion gets a new record and cannot inherit the prior values. A reference
held by an in-flight `FlsFree` callback delays record destruction and native
deletion until that callback returns. For deletion of the current fiber, the
deleting thread retains the list-owner reference and waits for those callbacks
before clearing its own TLS and invoking native `DeleteFiber`. A foreign
finalizer therefore cannot delete its still-running stack or clear the foreign
thread's unrelated TLS context.

`CreateThread` wraps the start function so an ordinary return runs FLS
rundown in user context. Routed `ExitThread` and `FreeLibraryAndExitThread`
run the same teardown before native exit or unload. `FlsGetValue` returns
NULL plus `ERROR_SUCCESS` for a valid empty bridge slot and NULL with an
error for an invalid index. Callback pointers are associated with their
allocation base at registration; a later unmapped/different allocation is
not invoked, and `FlsFree` reports `ERROR_INVALID_ADDRESS` in this case.
This is a best-effort stale-pointer guard, not a module pin or an unload
synchronization protocol.

## Contract difference and fixable policy

On this modern 32-bit Windows host, native `FlsAlloc(NULL)` followed
immediately by `FlsGetValue` on an unconverted thread returned NULL with
`GetLastError()==87` (`ERROR_INVALID_PARAMETER`). After explicitly setting
NULL, the same get returned NULL/Error 0. After fiber conversion, another
freshly allocated native slot returned NULL/Error 0, consistent with the
thread's FLS data being carried into the fiber. The bridge currently returns
NULL/Error 0 for every allocated but unset slot, even when no per-thread
record has yet been made. The [Microsoft `FlsGetValue` page](https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsgetvalue)
does not settle this exact never-initialized last-error case; pinned Wine and
ReactOS check for absent per-thread FLS data. This return/error policy is a
specific fixable item. A differential matrix across supported Windows
versions and Notepad++ plugin behavior should decide it before claiming full
FLS equivalence.

## Build and observed tests

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-fls.ps1`.
It builds `build/fls/FLSFIX.DLL` plus `build/fls/fls_smoke.exe`. The PE gate
requires i386 PE32, Win98 subsystem 4.10, no TLS/delay/CLR directories, and
imports only names in the extracted original Korean Win98 SE `KERNEL32.DLL`
export manifest. It rejects static `Fls*` imports. The host direct API-table
smoke passes separate values across fibers, thread-to-fiber value transfer,
FlsFree across fibers, callback reentry/retirement, generation reuse,
DeleteFiber callback, and callbacks after routed natural thread return,
`ExitThread`, and `FreeLibraryAndExitThread`. The later smoke revision also
checks `FlsFree` across a still-live worker thread, then reads that freed
index from the worker.

The first direct guest fixture run used `FLSFIX.DLL` SHA-256
`16d73d24b428153373bbded9d826c0260b3dc99caaa5cb8d36efdf214a5b611b`
and `FLSSM.EXE` SHA-256
`16610ec48061648267fbfff3c59c631edbfa6e59ed54211510eb94ac4a93ffe7`.
In the directly installed Korean Win98 SE guest, the COM1 remote shell
receipt `build/guest/receipt-fls-fixture.json` recorded exit code 0,
no timeout, and output SHA-256
`3caa76ec68871d662cc8987a4e20427ebbc461310234be94e217a282f113ffa8`.
The guest printed that original `Fls*` is absent and passed the direct bridge
slots/fibers/callbacks/thread-hooks test. The still-live worker extension was
added afterward and passed host and guest. Its `fls_smoke.exe` SHA-256 was
`af1153377d6e0636f7d61ec6763fe28fe41bf655e95319e612725026779a988d`.
The installed-guest receipt `build/guest/receipt-fls-fixture-expanded.json`
again reports exit code 0, no timeout, and the same output SHA-256 because
the added assertion is silent on success. This is stronger direct-fixture
evidence, not static-import or application evidence.

The final fixture build also preserves `ERROR_INVALID_ADDRESS` after
`FlsFree` suppresses a callback whose owning module has already unloaded.
`FLSFIX.DLL` SHA-256
`904bb1ad433bfba13824344812226a1a4e73eadcdeb4f4f3ebf0806e3554dc40`
and the same expanded `FLSSM.EXE` SHA-256
`af1153377d6e0636f7d61ec6763fe28fe41bf655e95319e612725026779a988d`
passed the actual Win98 guest again: `build/guest/receipt-fls-fixture-final.json`
records exit code 0 and the same output SHA-256. That successful path does
not exercise an unloaded-module callback, so its error preservation is
verified by source and host execution only.

## Concurrency and reentry corrections after the initial guest checkpoint

The expanded independent review found and corrected three lifetime errors:

1. `ConvertThreadToFiber` used to decrement the old thread-record reference
   under lock, unlock, then read `old->references`. An in-flight `FlsFree`
   could drop the final reference between those two steps and free `old`,
   producing a use-after-free or double free. The conversion now decides final
   destruction while holding the lock and does not read the old record after
   unlock. It also rejects conversion while its thread context is already
   exiting, because a callback must not unlink and destroy the record whose
   rundown is still executing. That rejection is a bounded safety policy,
   not a claim of identical native behavior for this unusual callback usage.
2. A current-fiber `DeleteFiber` could previously return when a foreign
   `FlsFree` held a reference. The foreign thread would later finalize it,
   clear its own TLS slot using the other thread's context, and invoke native
   `DeleteFiber` on an executing foreign fiber. Current-fiber deletion now
   waits on the original thread and performs TLS/native cleanup there.
   Microsoft's [DeleteFiber contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-deletefiber)
   requires self-deletion to terminate the current thread; returning while a
   different thread later frees its stack does not meet that contract.
3. Rundown previously copied every callback/value into a local array before
   invoking any callback. Callback A could free slot B and allocate a new
   generation, then the saved callback B would run after `FlsFree(B)` returned.
   One-at-a-time detachment and including DYING records in `FlsFree` allow
   pending B cleanup to finish before that slot is reused. This follows the
   [FlsFree callback contract](https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsfree)
   and avoids keeping stale callback work past a reentrant free.

Before changing the fixture source, three separate host modes demonstrated:

```text
/review-slot:    FAIL: FlsFree returned before pending rundown B callback
/review-convert: FAIL: thread-to-fiber conversion accepted during thread rundown
/review-delete:  FAIL: DeleteFiber(current) returned while foreign FlsFree held a reference
```

After correction, `tools/build-fls.ps1` passed its original host suite and PE
gate, all three regressions, and an additional 32-round two-thread
conversion/FlsFree contention test that checks preservation of another live
slot's value. The reference-transfer test exercises contention but does not
claim deterministic coverage of every instruction-level scheduling window.
Each regression is also selectable with the arguments above; the contention
case uses `/review-transfer`. Normal execution runs all four cases.

This corrected source checkpoint is:

- `src/m98_fls.c` SHA256:
  `85b270039bf6ea21d65e0cd6b0cfb30f9231260f03eee8602fe4caa6a7456256`.
- `FLSFIX.DLL` SHA256:
  `4bf539384e18a5d444aeb2c7cfe78e5f8d6c288bcbf0a9054bdb16b738a9785b`.
- `fls_smoke.exe` SHA256:
  `4c73e0aaab7bbc816352df5e7960122aa535820b3e13fdae3a955eb47d547ca0`.

Guest execution of this corrected checkpoint is a new handoff gate. The older
guest receipts above establish only their listed fixture hashes and do not
automatically validate these concurrency fixes.

## Remaining integration and behavior gaps

This is a direct API-table fixture. KernelEx `CORE.INI`, shared `m98wrap`,
static imports, real Notepad++ plugins, and application behavior have not been
verified for FLS. The routing must include the four FLS APIs **and** the
native fiber/thread lifecycle entries in the same backend. If original
`CreateThread`, `ConvertThreadToFiber`, `CreateFiber`, `DeleteFiber`, or exit
imports bypass the table, callbacks or fiber identity can be missed.

KernelEx core's `DisableThreadLibraryCalls` blocks a core natural-exit hook.
The API-library `DLL_THREAD_DETACH` is under loader lock and is deliberately
unused for application callbacks. Natural returns from native/unrouted
`CreateThread`, CRT `_beginthreadex`, `CreateRemoteThread`, direct native
`ExitThread`, forced `TerminateThread`, suspended threads that never start,
and process shutdown remain unproven. Cross-thread deletion of an executing
fiber, callback-triggered thread exit during an unfinished `FlsFree`, and
concurrent module unload during callback invocation also need targeted
semantics and safety work. The owner-allocation guard cannot prevent an
unload race after the guard but before the call. No arbitrary callback is
run from `DllMain`. These are concrete blockers to calling the lifecycle
foundation complete or marking FLS fully compatible.

In particular, the new current-fiber deletion wait covers references held by
foreign callbacks. A callback inside `FlsFree` that deletes its own current
fiber holds a reference whose release requires that same callback to return;
that unsupported reentrant thread-termination route can block the wait. Solving
it requires a cancellation/abandonment protocol for the interrupted `FlsFree`
operation and its retiring slot, not transferring TLS cleanup to another thread.
The independent fixture is not registered in the production provider or shipped
as a production FLS implementation while these lifecycle gaps remain.

## Reviewed fixture guest confirmation

The revised FLSFIX.DLL (`4bf539384e18a5d444aeb2c7cfe78e5f8d6c288bcbf0a9054bdb16b738a9785b`) and expanded probe (`4c73e0aaab7bbc816352df5e7960122aa535820b3e13fdae3a955eb47d547ca0`) ran in the directly installed Windows 98 SE guest. All four new regression lines and the existing fixture suite passed (exit 0; 591 output bytes). Receipt: `build/guest/receipt-fls-reviewed18.json`; output SHA-256 `aa910da1e53dbca07ded99390a17a8667e76f3e797fd0fc19be5f11deda5d7fe`. This confirms the stated direct fixture paths; the production-routing and lifecycle gaps above remain.
