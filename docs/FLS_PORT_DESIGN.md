# Fiber local storage and thread/fiber API bundle

Status: source review and native primitive probe. **No FLS wrapper has been
implemented or guest-validated.** This is a shared foundation for the four
`Fls*` imports in Notepad++ 8.9.8 and its plugins, and for later whole-API
coverage. The direct-import evidence is in `docs/NPP_IMPORT_TRIAGE_8_9_8.md`.

## Contract and source decisions

[Microsoft's fiber overview](https://learn.microsoft.com/en-us/windows/win32/procthread/fibers)
states that fibers on one thread share TLS, while FLS follows the selected
fiber. The [`FlsAlloc`](https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsalloc),
[`FlsGetValue`](https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsgetvalue),
[`FlsSetValue`](https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flssetvalue),
and [`FlsFree`](https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsfree)
contracts require process-wide indexes, a separate value for each fiber,
`NULL` for an empty valid slot, and an optional per-index callback. `FlsFree`
releases an index across **all** fibers and calls its callback for each
non-`NULL` value. The
[`PFLS_CALLBACK_FUNCTION`](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nc-winnt-pfls_callback_function)
contract also calls it on fiber deletion and thread exit. `FlsFree` does not
itself free pointed-to application memory; the callback or application does.

Pinned [Wine `kernelbase/thread.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/thread.c)
has the four public wrappers around `RtlFls*`, passes status to Win32 error,
and makes a successful `FlsGetValue` clear last error to `ERROR_SUCCESS`.
Pinned [Wine `ntdll/thread.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/ntdll/thread.c)
holds process-wide index/callback state, a list of per-fiber FLS records,
and runs callback/rundown in `RtlFlsFree` and `RtlProcessFlsData`.
The same Wine `kernelbase/thread.c` stores a fiber-specific `fls_slots`
pointer on `SwitchToFiber`, inherits thread FLS on `ConvertThreadToFiber`,
and calls FLS rundown from `DeleteFiber`.
Pinned [ReactOS `kernel32/client/fiber.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/client/fiber.c)
implements FLS with a process bitmap/callback array, per-fiber records,
`BaseRundownFls`, and `DeleteFiber` integration. Its converted fiber inherits
the thread's prior FLS data. Both upstream implementations use NT PEB/TEB
fields unavailable as a stable Win98 API, so this project needs independent
Win98 state and lifecycle hooks. No upstream implementation code is copied
by this design; any later copied code needs its per-file license notice.

The pinned [KernelEx KERNEL32 API list](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/Kernel32/_kernel32_apilist.c)
contains `CreateFiberEx`, but no `Fls*` entry. Its
[`CreateFiberEx_new`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/Kernel32/thread.c)
forwards to Win98 `CreateFiber` and does not implement all reserve/commit or
flag behavior. It is not a complete fiber lifecycle interception layer.

## Bundle inventory and order

The Korean Win98 SE OEM `KERNEL32.DLL` export manifest at
`benchmarks/win98se-ko-oem-native-exports-v1.json` is the source for the
native column. The process-wide FLS state and lifecycle hooks must be built
as one bundle: exporting the four new names alone cannot satisfy teardown.

| Surface | OEM Win98 | Role and port dependency |
| --- | --- | --- |
| `TlsAlloc`, `TlsFree`, `TlsGetValue`, `TlsSetValue` | Native | Thread-only storage. Useful for a thread record pointer, never as the FLS value map. |
| `GetCurrentThreadId`, `CreateThread`, `ExitThread`, `FreeLibraryAndExitThread`, `TerminateThread`, `ExitProcess` | Native | Thread identity/creation/exit paths; normal thread-procedure return and termination must be accounted for. |
| `CreateFiber`, `ConvertThreadToFiber`, `SwitchToFiber`, `DeleteFiber` | Native | Real Win98 fiber creation/selection/deletion. Wrapping lifecycle calls is needed for registration and callbacks. |
| `GetCurrentFiber`, `GetFiberData` | Header macros, no export | Read current fiber identity/data; the x86 macro reads the native thread information block. Verify on the installed guest before depending on layout. |
| `CreateFiberEx` | KernelEx only | Existing adapter has limited semantics; register fibers created through it and audit its flags. |
| `ConvertThreadToFiberEx`, `ConvertFiberToThread`, `IsThreadAFiber` | Neither OEM nor pinned KernelEx list | Later group members. They need real Win98 fiber-state behavior, not name-only aliases. |
| `FlsAlloc`, `FlsFree`, `FlsGetValue`, `FlsSetValue` | Absent | New stdcall `KERNEL32` API-table entries, with process-wide indexes and fiber-specific values. |

Other Win98 calls such as `WaitForSingleObject`, `CloseHandle`, heap functions,
critical sections, and interlocked operations support the implementation and
tests; they are not new exports. `GetProcAddress` and direct OEM imports need
to route to the same FLS/fiber lifecycle implementation, including app-local
DLLs. A KernelEx `CORE.INI` route for only the four FLS names leaves native
`DeleteFiber` calls unobserved. The implementation order is: verify native
identity; establish a process-wide state manager; intercept/create/register
fiber and conversion paths; prove teardown hooks; implement four FLS APIs;
then expose routes and run static-import/application tests.

## State model proposed for Win98

1. Maintain one locked process table of FLS indexes. Reserve index zero and
   use `FLS_OUT_OF_INDEXES` (`0xffffffff`) on allocation failure. A slot has
   `FREE`, `ACTIVE`, or `RETIRING` state, callback pointer, and a private
   generation. The initial capacity can target the older 128-slot contract
   (127 allocatable indexes), but must be checked against a current Windows
   reference and the selected apps before claiming full compatibility. Wine
   currently supports a larger index space; an index count is a compatibility
   limit, not just an implementation detail.
2. Maintain a separate `fiber_record` for each *native fiber instance* with
   its current values and generation stamps. A thread that has not converted
   has a distinct thread record keyed by `GetCurrentThreadId`. A converted
   thread's existing values move to its returned native fiber record. Values
   follow the fiber when it is switched or scheduled by another thread;
   `GetCurrentThreadId` alone is therefore not the fiber key.
3. Obtain the active native fiber address through `GetCurrentFiber` only after
   the native probe verifies Win98's x86 macro. Do not assume the raw value
   on an unconverted thread is a valid fiber pointer: a modern host reports
   `0x1e00` on *both* unconverted threads. Record conversion state from the
   routed `ConvertThreadToFiber`/`CreateFiber` path. A call from a preexisting
   untracked fiber needs either validated discovery or explicit failure until
   all lifecycle paths are covered. Never dereference an opaque fiber pointer
   merely to guess its structure.
4. `FlsSetValue` validates an active slot and writes only the current record.
   `FlsGetValue` validates the slot, returns `NULL` plus `ERROR_SUCCESS` for
   an empty valid slot, and returns `NULL` with a real error for an invalid
   index. `FlsAlloc` zero-initializes values across records. A `NULL`
   callback is permitted; no callback runs for a `NULL` value.
5. `FlsFree` atomically marks the index `RETIRING`, detaches and clears that
   index's values from every live record, then calls the captured callback
   once per non-`NULL` value *outside the manager lock*. It cannot make the
   index reusable until callbacks finish. Reentrant `FlsFree`/`FlsSetValue`
   on the retiring index fail cleanly; callbacks may allocate/use other
   indexes. Only afterward increment the generation and mark the slot
   `FREE`. A new allocation at the same numeric index sees only zero values.
   This is a proposed safe Win98 policy; callback order/reentrant details
   require differential Windows tests because the public contract does not
   specify them fully. Wine/ReactOS call callbacks while holding a global
   lock in some paths, so copying that locking pattern would risk reentry.
6. `DeleteFiber` must first mark the target record `DYING`, detach its values,
   invoke its callbacks outside the manager lock, and only then call native
   `DeleteFiber`. The current-fiber case must be handled before native
   `DeleteFiber`, which exits the thread. A record stores an internal serial
   as well as the pointer because `CreateFiber` may reuse a freed address;
   stale values cannot be inherited by a new fiber at the same address.
   Concurrent deletion of a fiber running on another thread is unsafe even
   in the documented Windows model and is not an accepted success case.
7. Thread exit needs one teardown path for its unconverted thread record and
   its selected fiber record. A routed `ExitThread` wrapper can run callbacks
   before native exit, and a `CreateThread` start thunk can catch ordinary
   thread-procedure return. However, those two wrappers alone may miss CRT or
   native internal paths. [`ExitThread`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread)
   normally sends DLL thread detach, but arbitrary application callbacks in
   `DllMain` run under the loader lock; it is unsuitable as the sole general
   teardown implementation. [`DllMain`](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain)
   also receives no thread detach on `TerminateThread` and only process
   detach on some unload/exit paths. Full coverage likely needs a tested
   KernelEx core or Win98 thread-start/shutdown hook, plus an explicit policy
   for forced termination. Until that hook is proven, thread-exit callback
   compatibility must remain **incomplete**.

The lock protects registration, slot states, and record lifetime. Callback
execution may reenter FLS or create/delete fibers, so callback work cannot
hold that lock. Detached records need refcounts or an equivalent deferred
free to survive nested operations. User callbacks may be in unloadable DLLs;
`FlsFree` during DLL unload must release their indexes before code disappears.
No callback may be invoked after its owning module has unloaded. The exact
Windows behavior for callbacks that set new values during teardown, and for
simultaneous index free and fiber deletion, must be measured rather than
guessed. A wrapper that merely returns success for these cases is not ready
for compatibility counting.

## Native probe and acceptance gates

Run `./tools/build-fls-native.ps1`. It builds
`build/fls-native/fls_native_probe.exe` from `tests/fls_native_probe.c` as a
32-bit PE with subsystem 4.10 and only exports listed in the pinned original
Win98 OEM `KERNEL32` manifest. The PE gate rejects any FLS import. The host
run passed: `GetCurrentFiber` on two unconverted threads was `0x1e00`, native
conversion changed it to the returned fiber address, a child had a distinct
address, native TLS changes were shared across those fibers, and a newly
created fiber reused the deleted child's address. The probe SHA-256 is
`8f35dec144b8d40a95b04150854ea593c4002caeb6a55fac67eb25212521dc9f`.

The same probe then ran in the directly installed Win98 SE guest through the
COM1 remote shell, exited 0 without timeout, and printed
`PASS: original KERNEL32 fiber identity and TLS sharing baseline`.
Both unconverted threads had raw fiber value **0**, unlike the host's 0x1e00.
Converted-main and child addresses matched GetCurrentFiber; TLS values were
shared across fibers; deleting and recreating a child reused its address.
Output was 515 bytes, SHA-256
`ac79b7ef0edba057d6aba6226487343d174fc21e92fe72bc275ddc58d7dbe543`.
This verifies baseline assumptions for the design, not an FLS implementation.

After the guest native baseline, the integrated FLS tests must cover two
fibers on one thread with different values; unconverted thread versus
converted fiber value transfer; a fiber switched to another thread; valid
empty `Get` versus invalid index; `NULL` and non-`NULL` callback values;
`FlsFree` across all fibers; slot reuse with cleared values; deleted fiber
callbacks; current-fiber deletion; natural thread return and explicit
`ExitThread`; callback reentry and concurrent free/delete. Each test must
record whether it called the API table directly, imported through KernelEx,
or exercised the real Notepad++/plugin path. Only guest-validated behavior
may be counted as compatible.
