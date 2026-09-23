# KernelEx thread lifecycle and FLS coverage

Status: source trace and bounded Windows 98 executable probes. The installed
`M98WRP19.DLL` route passes the tested EXE and system MSVCRT thread-exit paths;
general process-wide FLS thread-exit compatibility is **not established**.
The pinned KernelEx tree
is `third_party/KernelEx` at `31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`;
the project tree used for this trace was `fca60dfcc2917e8fc6ba7f045d1a9bc058b004ba`.
KernelEx is GPL-2.0; the new probe is original GPL-2.0-only code and copies
no KernelEx implementation.

## What the source actually hooks

| Area | Evidence | Consequence |
| --- | --- | --- |
| Core entry | `core/main.cpp:189` calls `DisableThreadLibraryCalls` from `PreDllMain`. | Core itself receives no ordinary `DLL_THREAD_DETACH` on which to attach a general FLS handler. |
| KEXBASES entry | `apilibs/kexbases/main.c:262-272` handles thread attach/detach, calling `thread_uninit` and `pti_uninit`. | These are DLL notifications under loader lock. Running arbitrary user FLS destructors here would permit reentry, loading, and deadlock at a prohibited time. The existing handlers do not run FLS callbacks. |
| API table | `apilibs/kexbases/Kernel32/_kernel32_apilist.c:69` registers `CreateThread_fix`; the table has no `ExitThread` or `FreeLibraryAndExitThread` override. | Existing KEXBASES adds no whole-thread exit interception. |
| Creation wrapper | `apilibs/kexbases/Kernel32/thread.c:116-131` substitutes a writable thread-ID pointer if needed, then invokes original `CreateThread` with the unchanged user start routine. | Its wrapper cannot see normal return from that routine. `CreateRemoteThread_new` can call this path for the current process at line 73, but it likewise adds no FLS lifecycle thunk. |
| Resolver | `core/resolver.cpp:52-143,494-595` chooses configuration by **calling module**. `core/thunks.cpp:32-101` supplies a caller MODREF for static imports and discovers the caller for dynamic resolution. | EXE and system MSVCRT imports can resolve differently. One EXE IAT measurement does not establish CRT routing. |
| Shared-address rule | `core/internals.h:33` defines shared as `>= 0x80000000`; `core/resolver.cpp:61` rejects API extension for such a calling module before per-module settings. | A CRT loaded in that range could not be routed by CORE.INI alone. Its actual guest base matters. |
| Native bypass | `core/resolver.cpp:766-785` implements `iGetProcAddress` against original exports; `core/kexcoresdk.cpp:329-331` exposes it as `kexGetProcAddress`. | The FLS provider needs an explicit original-API path, or a verified module-level no-route rule, for its own `CreateThread`/`ExitThread`/`FreeLibraryAndExitThread` calls. Routing them back to the same provider would recurse. |
| API-library exclusion | `core/SettingsDB.cpp:54-61,172-184` calls `add_apilib_excludes` after settings and marks every registered API-library path `LDR_KEX_DISABLE`. `core/resolver.cpp:104-109` applies process-wide module override before this per-module lookup when requested. | With ordinary settings, the registered `m98wrap` provider's own native imports should remain original. The process override flag is an exception; verify actual provider IAT targets after FLS routes are installed. |
| KERNEL32 patch table | `vxd/interface.h:30-40` lists eight entries for export resolution, KnownDLL, load tree, subsystem and resource handling. `vxd/patch_kernel32.cpp:419-483` writes those call references. | There is no native thread-start or thread-termination jump entry in this pinned revision. `FLoadTreeNotify` is a module load-tree hook, not a thread-exit hook. |
| Other VxD hook | `vxd/patch_ifsmgr.cpp:175` hooks `IFSMGR_CheckLocks_fixed`. | It does not solve thread teardown. |

The FLS backend wraps `CreateThread` with a start thunk, calls its own rundown
after a normal start-routine return, and has separate `ExitThread` and
`FreeLibraryAndExitThread` wrappers (`src/m98_fls.c:732-787`). The backend
comment in `src/m98_fls.h`
correctly forbids calling its rundown from DllMain. These wrappers cover only
calls that actually resolve through the FLS provider.

## Direct Windows 98 observations

The original-API V1 probe was run in the installed Windows 98 SE guest. Its
receipt is `build/guest/receipt-thread-lifecycle18.json`; the tested binary's
SHA-256 was `73d4faf952cfd52a14de3730fbbeb235d7ccce810ced9308132c83aadc8a3863`.
It exited 0 and the output SHA-256 was
`9ce5cb9fbd7fc996aaa4e5837831caff31b11c3eac7f5aee2ff8252719b63c72`.

| Observation | Guest value | Meaning at this revision |
| --- | --- | --- |
| Probe EXE `CreateThread` static and dynamic target | `0xBFA41C20`; MSVCRT's same pointer was identified as KEXBASES | KernelEx routes the EXE's `CreateThread` to `CreateThread_fix`. |
| Probe EXE `ExitThread` target | `0xBFF8A08E`, original KERNEL32 | No FLS teardown route yet. |
| Probe EXE `FreeLibraryAndExitThread` target | `0xBFF95CE9`, original KERNEL32 | No FLS teardown route yet. |
| System MSVCRT base | `0x78000000` | Below KernelEx's shared-address cutoff on this actual guest. The source's shared-module rejection does not itself block this CRT. |
| System MSVCRT `CreateThread` import | `0xBFA41C20`, owner `C:\WINDOWS\KERNELEX\KEXBASES.DLL` | The system CRT is a real KernelEx-routed caller for this import. |
| System MSVCRT `ExitThread` import | `0xBFF8A08E`, original KERNEL32 | Its exit path remained unwrapped. |
| `_beginthreadex` normal return | exit code `0x3579` | CRT creation and natural completion worked; no FLS callback was tested in V1. |

The Win98 `VirtualQuery`/`GetModuleFileNameA` owner lookup for the EXE's
KEXBASES address returned `unresolved`. The identical pointer in MSVCRT's IAT
did resolve to `KEXBASES.DLL`; that is the basis for its identification.

## Bounded callback test

`tests/thread_lifecycle_probe.c` V2 is built by
`tools/build-thread-lifecycle.ps1` into `build/thread-lifecycle/THRLIFE.EXE`.
The current V2 SHA-256 is
`8ace2b0df50ae9c14699bc429f0f1d3a14ff9478846c7e069ad19a215725b7e7`.
The build gate checks i386 PE32, console subsystem 4.10, no ASLR/NX/TLS/delay
imports/CLR, and that every static import is present in the original Korean
Windows 98 SE OEM KERNEL32 export manifest. The host run passed the native
thread paths and all three host FLS callbacks. The guest result below
independently checks Windows 98 routing.

V2 still prints EXE, MSVCRT, and any loaded `M98WRAP`/`M98WRP13`–`20` module
IAT targets. It resolves `FlsAlloc`, `FlsSetValue`, and `FlsFree` dynamically,
because they are absent from original Win98. If all three resolve, it places a
different FLS value in each of these threads and checks callback count, value,
and current-thread ID immediately after each thread handle is signaled:

1. EXE `CreateThread` routine returns normally.
2. EXE `CreateThread` routine calls `ExitThread` explicitly.
3. System MSVCRT `_beginthreadex` routine returns normally.

It then calls `FlsFree` and checks that no deferred callback appears only at
free time. A callback failure returns exit code 1. If any dynamic FLS API is
absent, it prints `FLS_ROUTE=absent_or_partial` and remains an observational
native routing probe.

The cold-booted Windows 98 SE guest ran V2 at `C:\M98LAB\THRLIFE.EXE` in the
completed 10/10 suite `build/guest/suite-lifecycle19-routed.json`. The V2 test
exited 0; its 1,906-byte output SHA-256 was
`b60f40b3dbfab4d4972311fa2c7b8cfa78b5650f7d6195c7df01d6b274bfc695`.
It resolved `CreateThread`, `ExitThread`, `FreeLibraryAndExitThread`, and all
three tested FLS APIs in the EXE to the installed
`C:\WINDOWS\KERNELEX\M98WRP19.DLL`. The EXE's natural return and explicit
`ExitThread` each invoked one callback, yielding counts 1 and 2. The system
`MSVCRT.DLL` loaded privately at `0x78000000`; its own `CreateThread` and
`ExitThread` IAT entries also pointed into `M98WRP19.DLL`. A natural return
from `_beginthreadex` invoked the third callback, and `FlsFree` left the
count at 3. In the same process, `M98WRP19.DLL`'s own `CreateThread` and
`ExitThread` IAT entries pointed to original `KERNEL32.DLL`, so this tested
provider instance did not recurse through its own route. The separate static
FLS import test `FLSI19.EXE` also exited 0 in that suite (output SHA-256
`416ff3b4bd2ff387b09b8b5a3ac7bc1508f2737bd9bcffc9fef02f79860af2f8`).
The V1 baseline and V2 routed results remain separate: V1 observed MSVCRT
`ExitThread` bound to original KERNEL32 before the new provider was installed.

## Independent FLS rundown race review

The initial FLS backend detached a value under its lock, then invoked its
thread-rundown callback after unlocking. A concurrent `FlsFree` could see no
remaining value and return while that callback was still running. The current
`src/m98_fls.c` reserves callbacks per slot and holds the retiring generation
until the reservation is released. It also tracks stack-owned cleanup frames
by native fiber and rejects deletion of a different fiber with a suspended
cleanup frame using `ERROR_BUSY`.

The separate original-import `tests/fls_rundown_race.c` covers both paths.
One worker blocks inside a thread-rundown callback while another calls
`FlsFree`; the test requires `FlsFree` to wait. A second case switches from
fiber A's callback to fiber B, attempts `DeleteFiber(A)`, checks `ERROR_BUSY`,
then resumes A and lets its callback finish. The standalone builder is
`tools/build-fls-rundown-race.ps1`; its PE gate checks both fixture DLL and
probe EXE against the original Win98 KERNEL32 OEM import manifest.

The revised race probe signals a `free_entered` event immediately before
`FlsFree`, and the parent waits for it before checking whether `FlsFree`
returned while the callback remained blocked. This removes the earlier
unbounded delay between creating the freeing thread and its first instruction;
the final narrow event-to-call scheduling interval remains a timing test.

Host PE gate and both bridge cases passed. `FLSFIX.DLL` SHA-256 was
`9bd96fb195091f4e5a29e905dddb8768346a1d1f12bb296fa82a49a98453733e`;
the revised `FLSRACE.EXE` SHA-256 is
`b525a71abe9c373b0451c42b6f4bbb1c0c4b99689b916a5e665f3e1f1de06bb1`.
The preceding EXE SHA-256
`09b8d54bee6bf93ffcfca328b13cbc4e068d9b77a02f523157e1c2cff022e7d2`
ran in the installed Windows 98 SE guest with exit code 0;
`build/guest/receipt-fls-rundown-race19-direct.json` records a 243-byte full
output with SHA-256
`4b49faeb228deceb1a48c858b500af9e2ad95e77a4dd4f20c6e7fe9797f4effa`.
It reported that `FlsFree` waited for the blocked callback, rejected deletion
of the suspended cleanup fiber and resumed it. Original Win98 FLS exports were
absent, so the guest native oracle was skipped. This is **direct fixture**
evidence for the preceding EXE.

The revised `b525a71a...` EXE was subsequently transferred and run in the
cold-booted routed guest suite `build/guest/suite-lifecycle19-routed.json`.
It exited 0; the complete 284-byte output SHA-256 was
`0650d39838a8e4dc61adb4f2e159ab975631588712f4d0824271719b91bb8875`.
Both its direct bridge case and dynamic `KERNEL32` FLS case observed that
`FlsFree` waited for the blocked rundown callback. Its direct bridge case
also rejected deletion of the suspended cleanup fiber and resumed that
callback. In this routed suite the dynamic `KERNEL32` lookup reaches the
installed provider, so it is a second route check, not an original Win98 FLS
oracle. Static import routing is separately evidenced by `FLSI19.EXE` above.

As a negative control, the same revised EXE was run against a fixture built
from the unmodified `fca60df` version of `src/m98_fls.c` in isolated
`build/fls-race-before` output, without editing production source. That
fixture's SHA-256 was
`4bf539384e18a5d444aeb2c7cfe78e5f8d6c288bcbf0a9054bdb16b738a9785b`.
`build/fls-race-before/receipt-negative-control.json` records exit code 1 and
output SHA-256
`9135038278225c849c1777d1e317bcbf21987bccc728f4e8aa89445165e77262`;
it reported that `FlsFree` returned while the callback remained blocked.

The per-slot reservation avoids early index reuse, but a callback on fiber A
can switch to fiber B on the same thread. If B calls `FlsFree` for A's slot,
the implementation treats A's reservation as local to avoid deadlocking B and
may return while A remains suspended; the slot stays retiring until A resumes.
An application that unloads A's callback DLL before resuming A remains outside
the tested safety boundary. The two cases above do not exercise that unload.

## Coverage and next implementation boundary

The installed 13-API FLS/fiber/thread route made this EXE and system MSVCRT
see the provider. V2 measured callbacks after a routed `CreateThread` normal
return, a routed `ExitThread`, and a system MSVCRT `_beginthreadex` normal
return. V1 showed that MSVCRT's exit route was absent at baseline. The
installed provider's own IAT pointed to original KERNEL32 in V2, consistent
with the pinned KernelEx `SettingsDB` API-library exclusion. Recheck that
binding for each route or provider change: a process-wide override
(`LDR_OVERRIDE_PROC_MOD`), registration/path mismatch, or preloaded provider
could defeat the exclusion and cause recursion.

Even a successful V2 proves only those three exercised paths. It does not
cover shared-address callers, original KERNEL32 internal calls that bypass its
export resolver, `CreateRemoteThread`, preexisting threads, `_beginthread`,
`TerminateThread`, process exit, or every CRT version. In particular,
`DLL_THREAD_DETACH` is not a valid fallback for invoking user callbacks because
the loader lock is held and callback code can load modules or reenter FLS.

A full natural-exit design for unrouted callers would need a version-matched
**shared** KERNEL32 thread-start/termination gateway, introduced through
coordinated VxD/core
patches rather than a per-process jump in shared KERNEL32 `.text`. The gateway
must run on the exiting thread in user context outside loader-lock callbacks.
For `FreeLibraryAndExitThread`, it must preserve the observed order: unload
the requested module, perform provider rundown, then enter native thread
detach, with the provider itself pinned during that path. A callback housed in
the module being unloaded is outside this tested safety boundary. The gateway
must identify the current process, look up a process-local FLS provider only
when loaded, guard recursive exit, and
fall through to the original termination path when no provider is present.
That requires a callable provider rundown export and stable per-process
registration. The patch must be keyed to exact Win98 KERNEL32 build signatures
and fail closed on an unknown build. The current eight-entry jump table and
VxD patch source provide no such hook. First locate and trace the actual
4.10.2222/2226 thread start and termination path in the installed OEM binary;
verify with a guest fixture that callbacks run before loader detach, on the
exiting thread, and without affecting another process. No core patch is
included here, and general FLS compatibility remains unclaimed.
