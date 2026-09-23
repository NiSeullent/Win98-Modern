# Win98 FLS and fiber/thread lifecycle bridge

Status: thirteen API entries are linked in `M98WRP19.DLL` and explicitly
routed in the installed Win98 guest. Direct, static-import and system MSVCRT
callback tests passed. This is a supported contract subset; the complete
Windows thread/fiber lifecycle and application compatibility remain incomplete.

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
rundown in user context. Routed `ExitThread` runs the same teardown before
native exit. `FreeLibraryAndExitThread` unloads first and then enters routed
exit, matching the reviewed native order. The provider itself must stay
loaded throughout. `FlsGetValue` returns
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

Guest execution of this historical corrected checkpoint passed, as recorded
in the reviewed-fixture confirmation below. Those receipts establish only
their listed hashes and do not validate later abandonment changes.

## Routed callback termination and retirement reservations

The next review reproduced unfinished retirements when an `FlsFree` callback
called routed `ExitThread`, `FreeLibraryAndExitThread`, or current-fiber
`DeleteFiber`. The callback's held record reference and RETIRING slot used to
be released only when that callback returned. Termination prevented that
return; current-fiber deletion could then wait forever on its own reference.
The pre-correction host fixture reported missing/repeated retirement callbacks
for exit/unload, a two-second owner-thread timeout for current-fiber deletion,
and an incorrect exit status when a later cleanup callback requested exit again.

The corrected bridge links each active retirement/rundown continuation to its
thread context before invoking user code. Routed termination drains these
continuations while the original stacks still exist. A continuation releases
its current callback reference exactly once, drains remaining values, retires
the captured generation, and then proceeds with record/thread cleanup. Normal
callback return uses the same release path. Nested `FlsFree` and a callback
which requests termination again resume the top interrupted continuation;
the eventual native exit never returns into its abandoned C frame. A rundown
advances its saved slot cursor before invoking user code, so an interrupted
callback is not repeated.

A separate deterministic review exposed an additional foreign-thread race:
rundown could detach a value, unlock, and start its callback after another
thread's `FlsFree` had already returned and made the slot reusable. Each slot
generation now reserves detached callback work under the same lock. Retirement
waits outside the lock for foreign reservations. Callback return or routed
abandonment releases that reservation exactly once. A rundown callback may
free its own index without waiting on itself: it completes retirement, while
the slot remains RETIRING until that local callback returns or is abandoned.
This defers reuse of that index inside its own callback; it does not invoke an
old-generation callback after the slot has been reassigned.

Continuations still occupy their invoking native fiber's stack. A global list
under the FLS lock records that fiber identity, including frames owned by
another thread context. Deleting a different fiber with any such live frame
now preserves the fiber and sets `ERROR_BUSY`; it does not free a stack still
referenced by cleanup. Normal callback completion removes its frame by
identity. Full deletion/abandonment of suspended callback fibers is a remaining
compatibility feature, not implemented by this guard. Moving a paused callback
frame to another thread or terminating an OS thread while another resumable
fiber owns its cleanup frames remains outside the validated lifecycle subset.

Two Win98 integration details are also corrected:

- The native Win98 `CreateThread` import receives a local writable thread-ID
  output when the application passes NULL. A native creation error is saved
  before freeing the thunk allocation and restored afterward.
- `FreeLibraryAndExitThread` now calls `FreeLibrary`, then routed `ExitThread`.
  [Pinned Wine `kernelbase/thread.c`, lines 180–184](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/thread.c#L180-L184)
  and [ReactOS `kernel32/client/loader.c`, lines 507–530](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/client/loader.c#L507-L530)
  unload before exit. The host's independent marker-DLL probe likewise saw the
  module absent during FLS teardown. The provider must be lifetime-pinned;
  unloading the last reference to the executing fixture/provider is unsupported.

The pinned Wine `RtlFlsFree`/`RtlProcessFlsData` bodies and ReactOS
`FlsFree`/`BaseRundownFls` bodies were re-read for this change. Their NT
PEB/TEB, locking, and exception-unwind mechanisms do not supply a Win98
abandonment protocol. These continuation and reservation mechanisms are new
project code, with no copied upstream implementation body.

### Host evidence and guest handoff

The final `tools/build-fls.ps1` host run and original-Win98 PE/import gate pass.
Besides the earlier four regressions, the normal suite now checks a rundown
callback freeing its own index, deferred reuse until callback return, and
seven bounded child processes:

| Child mode | Verified bridge behavior |
| --- | --- |
| `-normal` | Nested `FlsFree` returns and both indices become reusable. |
| `-exit` | Nested callback exits its thread; remaining callbacks run once. |
| `-unload` | Callback unloads an extra fixture reference and exits. |
| `-delete` | Callback deletes its current fiber without waiting on itself. |
| `-repeat` | A remaining callback requests exit again during abandonment. |
| `-foreign` | Thread cleanup waits for a foreign in-flight record reference. |
| `-rundown` | A natural rundown callback begins nested free, whose callback exits. |

Every child verifies surviving foreign-record callbacks, exact invocation
counts, retirement completion, and post-termination slot reuse. Parent process
waits are bounded at eight seconds; owner-thread completion is bounded at two
seconds. `/term-bridge` runs only these seven cases; `/review-self-free` runs
the self-free reservation case. Timeouts fail the bridge suite rather than
leaving an indefinitely blocked test process.

`/term-native` observes the same seven scenarios using dynamically obtained
native host APIs. Normal, exit, unload, delete, and repeated exit completed.
The foreign-reference scenario also completed, but native Windows allowed the
owner to exit before the foreign callback was released; the bridge deliberately
waits to protect its record/stack lifetime. Nested termination started from an
already-running native rundown did not finish within two seconds and is
recorded as observation exit 124. These two differences are not described as
strict native equivalence.

The independent `tools/build-fls-rundown-race.ps1` builds
`tests/fls_rundown_race.c` and verifies both blocked foreign-rundown retirement
and the suspended-fiber deletion guard. Its host bridge tests pass, and the
dynamic native comparator also waits for the blocked rundown callback. Its
Win98 PE32/OEM import gate passes. Source/binary freeze for the new handoff:

| File | SHA-256 |
| --- | --- |
| `src/m98_fls.c` | `cbdee3c2224760f3817631471e90aea43ee4c97d0922440c492ee5af95d67139` |
| `src/m98_fls.h` | `cb71bc1a9680976840cccc6afef1e0fd85c950f3444acd16cdefefe78144f8d7` |
| `tests/fls_smoke.c` | `a7efa4186043498a589cb04e2593681fdf63fb180b196adffb129d7b75829950` |
| `build/fls/FLSFIX.DLL` | `9bd96fb195091f4e5a29e905dddb8768346a1d1f12bb296fa82a49a98453733e` |
| `build/fls/fls_smoke.exe` | `b676d56546a37c8f9f612fb2cdfbe2b8faed372306fce7b667d6a934a8d0d5cd` |
| `build/fls-rundown-race/FLSRACE.EXE` | `09b8d54bee6bf93ffcfca328b13cbc4e068d9b77a02f523157e1c2cff022e7d2` |

The independent race build emits the same fixture DLL hash. The following
root-agent direct guest receipts now cover the frozen fixture and smoke:

- `build/guest/receipt-fls-final19-direct.json`: `C:\M98LAB\FLSSM19.EXE`,
  exit 0, no timeout, 1,395 output bytes, output SHA-256
  `669b51d662713a22c6af16dbf5380ce6f680603e67e62fa93a7ecf4acbe163ec`.
  The complete fixture suite, self-free reservation case, and all seven
  bounded termination children pass.
- `build/guest/receipt-fls13-final19-direct.json`: `C:\M98LAB\FLSD19.EXE`,
  exit 0, no timeout, 122 output bytes, output SHA-256
  `712bcd84c2b249ea301ed292e8ef5622b7ae8267dbfa45c189134842d30cfce9`.
  The independent 13-API direct-table probe passes and observes the marker
  module **absent** during free-exit FLS cleanup, matching the native host.
- `build/guest/receipt-fls-rundown-race19-direct.json`: the race probe hash
  `09b8d54bee6bf93ffcfca328b13cbc4e068d9b77a02f523157e1c2cff022e7d2`,
  exit 0, no timeout, 243 bytes, output SHA-256
  `4b49faeb228deceb1a48c858b500af9e2ad95e77a4dd4f20c6e7fe9797f4effa`.
  Foreign-rundown retirement waits and the suspended-fiber guard pass.

The independent reviewer subsequently strengthened the race probe with an
event immediately before the freeing thread calls `FlsFree`. Its new EXE
SHA-256 is `b525a71abe9c373b0451c42b6f4bbb1c0c4b99689b916a5e665f3e1f1de06bb1`;
the fixture DLL is unchanged. This stronger probe passes on the host and fails
against the isolated pre-fix fixture (`4bf539...9785b`) with an early-return
diagnostic. The earlier race guest receipt above does not cover that revised
probe; its guest rerun is a separate gate.

## Remaining integration and behavior gaps

The installed provider now routes the four FLS APIs **and** nine native/Ex
fiber/thread lifecycle entries to the same backend. Real Notepad++ plugins
and application functionality remain unverified. If original
`CreateThread`, `ConvertThreadToFiber`, `CreateFiber`, `DeleteFiber`, or exit
imports bypass the table, callbacks or fiber identity can be missed.

KernelEx core's `DisableThreadLibraryCalls` blocks a core natural-exit hook.
The API-library `DLL_THREAD_DETACH` is under loader lock and is deliberately
unused for application callbacks. Natural returns from native/unrouted
`CreateThread`, `CreateRemoteThread`, direct native
`ExitThread`, forced `TerminateThread`, suspended threads that never start,
and process shutdown remain unproven. `ConvertFiberToThread` and an exported
`IsThreadAFiber` are not implemented. Nonzero `FIBER_FLAG_FLOAT_SWITCH` and
independent stack commit/reserve sizes remain unsupported. Native threadpool
workers created inside the provider currently bypass the routed `CreateThread`
thunk and do not yet have linked FLS teardown. Cross-thread deletion of an
executing fiber, full suspended-fiber abandonment, callbacks which escape
through raw native exit/SEH/longjmp, and concurrent module unload during
callback invocation also need targeted semantics and safety work. The
owner-allocation guard cannot prevent an
unload race after the guard but before the call. No arbitrary callback is
run from `DllMain`. These are concrete blockers to calling the lifecycle
foundation complete or marking FLS fully compatible.

The direct fixture's 13 API entries form a tested supported subset, not the
complete FLS/fiber/thread family. Production provider registration and static
application routing require separate evidence; their status must not be
inferred from the independent fixture or host tests.

## Reviewed fixture guest confirmation

The revised FLSFIX.DLL (`4bf539384e18a5d444aeb2c7cfe78e5f8d6c288bcbf0a9054bdb16b738a9785b`) and expanded probe (`4c73e0aaab7bbc816352df5e7960122aa535820b3e13fdae3a955eb47d547ca0`) ran in the directly installed Windows 98 SE guest. All four new regression lines and the existing fixture suite passed (exit 0; 591 output bytes). Receipt: `build/guest/receipt-fls-reviewed18.json`; output SHA-256 `aa910da1e53dbca07ded99390a17a8667e76f3e797fd0fc19be5f11deda5d7fe`. This confirms the stated direct fixture paths; the production-routing and lifecycle gaps above remain.

## Provider19 installed guest checkpoint

The directly installed Korean Win98 SE guest cold-booted with current NEM
hardware acceleration and provider SHA256
`ee96a7d5dfda768f21eb0098b8dafdd4080a018847329e5e145d8086b7a46cd4`.
CORE.INI SHA256 is
`a4a3e8e6286321a5a80a96d62bb2acc2386ba3ba384a780ef3c93f4e4c433598`.
Its 114 explicit routes cover 38 names in three profiles, including all thirteen
FLS/lifecycle entries. The KERNEL32 table contains 89 names.

`build/guest/suite-lifecycle19-routed.json` passed all ten tests. The static FLS
probe passed (output SHA256
`416ff3b4bd2ff387b09b8b5a3ac7bc1508f2737bd9bcffc9fef02f79860af2f8`),
including unload-before-exit ordering. The independent revised race probe also
passed through both the fixture and dynamically resolved installed FLS calls
(output SHA256 `0650d39838a8e4dc61adb4f2e159ab975631588712f4d0824271719b91bb8875`).
The same race executable failed against the pre-fix fixture on the host,
confirming that the regression distinguishes the fix.

THRLIFE V2 observed EXE and system MSVCRT CreateThread/ExitThread imports routed
to M98WRP19, while the provider's own imports resolved to original KERNEL32.
Natural return, explicit ExitThread and MSVCRT _beginthreadex each ran their
callback on the expected thread exactly once; final count was three. Output
SHA256: `b60f40b3dbfab4d4972311fa2c7b8cfa78b5650f7d6195c7df01d6b274bfc695`.
This proves those ordinary runtime paths, not raw kernel entry or every CRT.

The complete direct fixture suite, including seven bounded termination children,
also passed before installation (`receipt-fls-final19-direct.json`, output
SHA256 `669b51d662713a22c6af16dbf5380ce6f680603e67e62fa93a7ecf4acbe163ec`)
and after routing (within the ten-test suite). Notepad++ advanced beyond FlsAlloc
but still fails to start at USER32.RemoveClipboardFormatListener.
