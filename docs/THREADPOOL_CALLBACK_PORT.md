# Threadpool callback lifetime family

The existing work-object backend now implements seven more KERNEL32 entries:
`CallbackMayRunLong`, `DisassociateCurrentThreadFromCallback`,
`LeaveCriticalSectionWhenCallbackReturns`, `ReleaseMutexWhenCallbackReturns`,
`ReleaseSemaphoreWhenCallbackReturns`, `SetEventWhenCallbackReturns`, and
`TrySubmitThreadpoolCallback`. Together with the five work APIs, this is a
12-entry module; timers, waits, I/O, custom pools and cleanup groups remain
in the catalogue backlog.

## Source analysis and implementation

Reviewed Wine commit `df15af3652511150490934682202d45af892f887`,
`dlls/ntdll/threadpool.c` and `dlls/kernelbase/thread.c`, and ReactOS commit
`9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8`,
`sdk/lib/rtl/threadpool.c` and
`dll/win32/kernel32/kernel32_vista/threadpool.c`. Source comments identify
the reviewed routines. This is independently written GPL-2.0-only Win98 code,
using native events, semaphores, critical sections and threads, with no NT or
Unix runtime dependency.

Both upstreams keep one deferred resource of each kind in a callback
instance. After callback and finalization return, cleanup runs in this order:
critical section, mutex, semaphore, event, library. As in those implementations,
failure of a native handle operation stops subsequent cleanup. Registering a
deferred action neither releases its resource nor acquires a new reference.

`DisassociateCurrentThreadFromCallback` releases work waiters by removing the
callback's association. It retains the callback's work reference through its
actual return, finalization and deferred cleanup. Closing the work object while
that callback is still executing therefore cannot free its state early.
This follows Microsoft's documented
[disassociation contract](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-disassociatecurrentthreadfromcallback).

`CallbackMayRunLong` marks the current callback and grows the default pool when
no idle worker remains, subject to a 500-worker cap and real thread-creation
success. A control thread retries failed growth at 200 ms intervals even if all
workers are blocked. It runs no application callbacks. The default pool starts
with four workers. Original Win98 `CreateThread` requires a writable thread-ID
pointer, which all creation paths supply. Exhaustion/retry is not yet verified
by fault injection, and maximum capacity is not a guest performance claim.

`TrySubmitThreadpoolCallback` uses the same lifetime and cleanup backend, with
the two-argument simple-callback ABI and automatic caller-reference release.
The supported environment also honors the long-function flag and a finalizer.

## Verification

`tools/build-threadpool.ps1` builds isolated, integrated and ordinary static
KERNEL32-import versions of the callback test. The static version also runs
against native Windows on the host. `tests/check_threadpool_pe98.py` verifies
the exact twelve static imports and verifies that the fixture's other imports
exist in the original OEM Win98 export manifest.

The shared test checks deferred resource ownership before/after return,
exact semaphore release count, deferred DLL unloading, disassociation followed
by Close while the callback is still active, simple-callback finalization, and
four blocked long callbacks while additional work progresses. The earlier
work tests still cover parallel execution, queue cancellation, queued Close
and active Close. Host native comparison and isolated tests passed.

An earlier host oracle checked `GetModuleHandleA("TPMARK.DLL")` immediately
after `WaitForThreadpoolWorkCallbacks` and `CloseThreadpoolWork`. Over 20 native
Windows runs, the five-API static probe failed 3 times at a combined
callback/unload assertion; the callback-family static probe failed twice at
its explicit marker-unload assertion. This exposed a timing assumption in the
test: the public wait contract covers callback completion, while deferred DLL
unloading can finish later. The five-API probe now checks the callback count
separately. Both probes register an event with `TPMARK.DLL`; its
`DLL_PROCESS_DETACH` signals that event when deferred `FreeLibrary` actually
starts final unload. They wait for that signal and then separately observe
module absence with a bounded loader query. This is a completion signal from
the loader path, not a retry of the entire test after failure. The rebuilt
native five-API and callback probes each passed 20/20 host runs, and the PE98
native-import gate passed. These new event-based binaries still require their
own Win98 guest run. That run subsequently passed through static KERNEL32
imports after a hardware-accelerated cold boot with `M98WRP18.DLL`, SHA-256
`0da854a1a6ff42296e2e5c7fda92e07a660351a5b14b568f18c2846c609c2f3e`.
The static callback output hash was
`b5dc528c1ebb56f3f27514fef6c923fff2e2546b2b400896af0720e88d8178ae`.
Exact test and supporting marker hashes are in the guest evidence registry.

Independent review also found a process-termination deadlock risk: a killed
worker may own a critical section forever when DllMain runs. The production
wrapper now skips all lock cleanup on process termination, and the threadpool
fixture passes this termination distinction to its backend. `TPEXIT.EXE`
deliberately holds the backend lock in a child worker; the parent verifies
termination within a deadline, then separately checks ordinary detach.
Host and directly installed Win98 runs passed; guest output SHA-256 was
`2e7ccc36c6d05b97981a3ed04ce08b012822fb558a394429d9ec2080c46fb512`.
This regression targets the backend termination branch, not all OS shutdown
or manual-unload behavior.

The directly installed Win98 SE guest also passed the isolated callback test
and the existing work test. Callback output SHA-256:
`27266e84f2cffa287ef5242607c53bebad151c5fd4b20d476c21a4f6a29cc9a6`.
Exact integrated/static provider receipts, once collected, are recorded in
`benchmarks/api-guest-evidence-v1.json`; isolated success alone is not import
routing proof.

## Remaining contract work

- Custom pools, cleanup groups, timer/wait/I/O objects, activation contexts,
  callback priorities, and RaceDll environment ownership are not implemented.
- The provider must remain loaded for the process lifetime. Arbitrary unload
  of the isolated fixture with live workers is unsupported. DllMain never
  joins threads while holding the loader lock.
- Resource exhaustion, invalid-handle exception/fail-fast equivalence and
  shutdown during callbacks still need dedicated contract work.
- A test PASS validates these cases, not full threadpool or app compatibility.
