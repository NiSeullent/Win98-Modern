# One-time initialization API batch

This module implements the complete four-function public Win32 one-time
initialization family through one shared pointer-sized state machine. It replaces
the old isolated `InitOnceExecuteOnce` implementation instead of installing three
new functions with unrelated state. A measured Notepad++ loader failure identified
the missing Begin function; the unit of this port is the complete initialization
family, including dynamic initialization, manual completion, callback completion,
failure retry, and asynchronous contender behavior.

## API inventory and dependencies

| Public KERNEL32 API | Module implementation | Shared behavior |
| --- | --- | --- |
| `InitOnceInitialize` | `m98_InitOnceInitialize` | Initializes caller-owned storage to the same zero state used by `INIT_ONCE_STATIC_INIT` |
| `InitOnceBeginInitialize` | `m98_InitOnceBeginInitialize` | Claims a synchronous attempt, joins an asynchronous attempt, or reads the published result |
| `InitOnceComplete` | `m98_InitOnceComplete` | Publishes an aligned context exactly once or releases a failed synchronous attempt for retry |
| `InitOnceExecuteOnce` | `m98_InitOnceExecuteOnce` | Executes the callback through the same Begin/Complete state transitions |

The implementation is in `src/m98_initonce.c`, with declarations in
`src/m98_initonce.h`. The build asserts that `INIT_ONCE` occupies one 32-bit word.
The low two bits encode uninitialized, synchronous pending, complete, or
asynchronous pending. Completed context data uses the remaining bits.

Dependencies are original Win98 `Sleep`, `GetLastError`, `SetLastError`, and
interlocked operations. The compiler emits the interlocked instructions directly;
the fixture DLL imports only those three native functions. There is no heap
allocation, per-process state registry, event handle, NT runtime dependency, or
required DLL attach/detach initialization. Interlocked reads and transitions
provide publication barriers for the context and its initialized data.

Synchronous contention yields with `Sleep(1)` before rechecking the state. This
works with the existing Win98 scheduler and does not spin continuously while its
initializer needs the CPU. The wait interval and fairness differ from NT keyed
events; there is no guarantee that the oldest waiter initializes next. The tests
exercise host parallel threads, but this does not establish Win98 kernel SMP
support or replace the separate ShizukuDOS multicore work.

## Public source analysis and licensing

The new module is independently written GPL-2.0-only project code. It does not
copy the upstream NT waiter-list implementation.

- Wine, pinned commit `df15af3652511150490934682202d45af892f887`:
  [`dlls/ntdll/sync.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/ntdll/sync.c),
  `RtlRunOnceInitialize`, `RtlRunOnceBeginInitialize`, `RtlRunOnceComplete`, and
  `RtlRunOnceExecuteOnce` were read. Its atomically published tags, callback retry,
  reserved context bits, and synchronous versus asynchronous transitions were
  compared with the Win98 design. Its stack-linked keyed-event waiters cannot be
  carried over through an import-name wrapper.
- Wine's
  [`dlls/kernelbase/sync.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/sync.c)
  was read for the public BOOL wrappers: Begin sets `pending` only on success;
  failed Begin/Complete map NT errors to Win32 errors. The new implementation
  returns the corresponding Win32 errors directly.
- ReactOS, pinned commit `9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8`:
  [`sdk/lib/rtl/runonce.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/sdk/lib/rtl/runonce.c)
  was read in full. It identifies itself as derived from Wine and confirms the
  same NT-specific keyed-event dependency and state transitions. ReactOS's
  [`modules/rostests/apitests/kernel32/InitOnce.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/modules/rostests/apitests/kernel32/InitOnce.c)
  supplies additional observed contracts: failed `CHECK_ONLY` preserves outputs,
  sync/async Begin mismatch reports `ERROR_INVALID_PARAMETER`, and a losing
  asynchronous completion reports `ERROR_GEN_FAILURE`.
- Microsoft's official
  [one-time initialization overview](https://learn.microsoft.com/en-us/windows/win32/sync/one-time-initialization),
  [Initialize](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initonceinitialize),
  [Begin](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initoncebegininitialize),
  [Complete](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initoncecomplete),
  and [ExecuteOnce](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initonceexecuteonce)
  define the batch's ABI and valid usage. The code also checks reserved flags
  against native host behavior. The pinned Wine/ReactOS implementations accept
  some extra flags or completion mode mismatches that the current native host
  rejects; this port implements the stricter documented/native validation.
  Repeated native contention tests also exposed `ERROR_ALREADY_EXISTS` for a
  completion that loses its compare/exchange after observing async pending; this
  differs from a subsequent completion that first observes the completed object.

## Behavioral guarantees and limits

- All four functions operate on the same object. Synchronous manual and callback
  callers may contend on it and receive the same published context.
- A callback which returns FALSE releases the attempt and preserves its own
  `LastError`; waiting callers may retry. A failed manual completion must use
  `INIT_ONCE_INIT_FAILED` and a NULL context.
- `CHECK_ONLY` never starts initialization or waits. Before completion it returns
  FALSE / `ERROR_GEN_FAILURE` without modifying either output. On completion it
  returns `pending = FALSE` and the stored context, including a valid NULL context.
- All asynchronous contenders may obtain `pending = TRUE`. Exactly one successful
  `INIT_ONCE_ASYNC` completion publishes its context. A later completion that
  observes the completed state fails with `ERROR_GEN_FAILURE`; a simultaneous
  pending-state CAS loser fails with `ERROR_ALREADY_EXISTS`, matching the native
  host. Both can then read the winner using `CHECK_ONLY`.
- An active synchronous attempt cannot be joined/completed asynchronously, and
  vice versa. Once completed, either Begin mode can read the result. Combined
  `ASYNC | CHECK_ONLY`, unknown flags, an async failure reset, or a failed
  completion carrying data return `ERROR_INVALID_PARAMETER`.
- All three misalignments in the two reserved context bits are rejected. A
  rejected Complete does not silently publish data or release the active attempt.
- Storage must be correctly aligned and remain valid. Objects must not be copied,
  moved, or reinitialized while another thread uses them. Recursive synchronous
  initialization and owner termination do not acquire recovery semantics.
- Invalid-use exception fidelity is not claimed: calling ExecuteOnce on an active
  asynchronous object returns FALSE / `ERROR_INVALID_PARAMETER` here. The current
  native host instead terminated the isolated probe with `0xC00000F0`; a successful
  callback supplying a misaligned context similarly terminates native execution
  with `0xC00000F1`, while this module returns FALSE and leaves the attempt pending.
  The normal static-import suite omits this invalid ExecuteOnce case, while the
  direct suite checks that it fails without executing the callback. This boundary
  must not be described as exact compatibility for every invalid caller input.

## Reproduction and evidence

Run `./tools/build-initonce.ps1` from the repository root. It builds independent
fixture, direct, integrated, and static-import artifacts. All builds have a PE32
i386 / subsystem 4.10 gate, reject TLS/delay/CLR and ASLR/NX features, and check
native imports against the pinned original Korean Win98 SE export manifest.
Only the static-import executable is permitted to import the four new APIs.

The same behavioral suite is used for direct and static imports. In addition to
the parameter, retry, alignment, and mixed-interface cases above, it runs eight
rounds each of twelve simultaneous synchronous and asynchronous workers: 192
worker executions per binary. The synchronous test initially holds the object,
verifies that contenders cannot finish early, then fails that owner and checks
that exactly one of the mixed manual/callback callers initializes it. The async
test holds all contenders behind a completion barrier and checks that exactly one
completion wins. All thread joins and barriers have five-second failure bounds.

Host verification passed on 2026-09-24:

```text
PASS: InitOnce fixture and probes PE32/Win98/native-import gate
PASS: fixture InitOnce four-API contracts and 192 worker races
PASS: static KERNEL32 InitOnce four-API contracts and 192 worker races
```

The second line exercises this implementation. The third line exercises the
host's original KERNEL32 implementation with the same valid-use test cases. The
final fixture and native suites each passed four consecutive runs after the
observed asynchronous CAS error distinction was implemented. The
integrated probe was then linked with the complete module and passed the
host suite; the subsequent guest integration results are recorded below.

| Artifact in `build/initonce` | SHA-256 |
| --- | --- |
| `ONCEFIX.DLL` | `f60cb17864e49864e0ded597e8b884f7fb070dd8cfca473a8c4d9ce98a5a9855` |
| `initonce_smoke.exe` | `90d35daa5d163f47b9ad1c0d18e749c8c7ae2b48b7e007c091e131c68a72c10b` |
| `initonce_import_probe.exe` | `eda1117c4b288eb8d9961496ae52b575dfd95888350a483e65951564f6750ea6` |
| `initonce_integrated_probe.exe` | `78f69ef6c8c1f0016c80cd2be5047fe0a31bfda6c0825c6e50a1cf6d5b6b1729` |

The directly installed Korean Windows 98 SE guest passed all three routes:
fixture direct calls, integrated table direct calls, and ordinary KERNEL32
static imports after a normal shutdown and accelerated cold boot.

| Guest route | Exit | Output SHA-256 |
| --- | --- | --- |
| `ONCESM.EXE` + `ONCEFIX.DLL` | 0 | `8066ca6e57ccf0282e832a0e2a86f5b0660219b8b193fca98b51528deff2a044` |
| `ONCEINT.EXE` + app-local `M98WRAP.DLL` | 0 | `822300abcc0cf017fd89c8648832d61648ce93cc262141512cfebe0381f60ef5` |
| `ONCEIMP.EXE` through installed `M98WRP16.DLL` | 0 | `c0356210364223dac3ebe0cb8068f166d6694eadae3950326c6f67e08a9787ac` |

Each route ran the four-API contract suite and 192 worker races. The installed
provider and app-local integrated DLL were SHA-256
`e519192a39ca04f5bc2563a9fd42ac3d4d406209b4bd024ded481a3c3d9714b9`.
The installed CORE.INI SHA-256 was
`bd443e91dfa00a49d30dc870441c556b123e1e7dc0bce939b8c05fb39f647d97`;
it adds the four InitOnce routes in three profiles alongside the existing
NLS/threadpool routes. `vm/run.ps1` confirmed the current boot's NEM hardware
virtualization backend. These are focused contract results; the invalid-use
exception differences above remain. The receipt is also linked from
`benchmarks/api-guest-evidence-v1.json` to the complete API catalogue.

The app regression now passes the InitOnce import names but stops at
`KERNEL32.InitializeSListHead`; Notepad++ 8.9.8 still does not start.

## Integration handoff

1. Include `m98_initonce.h` in `src/m98wrap.c`.
2. Remove the old private `m98_init_once` type, callback typedef, and static
   `m98_InitOnceExecuteOnce` definition. Its public table name will resolve to the
   new common module. No SRW or condition-variable code needs to move.
3. Link `src/m98_initonce.c` into every build of `m98wrap.dll`.
4. Register the four table entries in sorted order: BeginInitialize, Complete,
   ExecuteOnce, Initialize. ExecuteOnce already exists, so the net count is +3.
5. No DllMain hooks are needed. Update the table-count smoke and release source
   allowlist through the root integrator's shared-file ownership.
6. On Win98, copy `ONCEFIX.DLL` and `initonce_smoke.exe` together and run the smoke
   from that directory. After integration, run `initonce_integrated_probe.exe`
   beside `M98WRAP.DLL`. After versioned provider installation and a full guest
   restart, run `initonce_import_probe.exe` to verify actual KERNEL32 import routes.

Add the source paths and the cited upstream review to `THIRD_PARTY.md` during
integration. Do not add the private downloaded source snapshots or host-only
scratch probes from `build/` to a public package.
