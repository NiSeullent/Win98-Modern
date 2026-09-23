# Sequenced singly linked list API batch

This module implements the shared x86 SList state transitions used by seven
KERNEL32 functions and eight NTDLL functions. All operations use the caller's
eight-byte header. There is no list registry, capacity ceiling, allocation,
process lock, required DLL initialization, or success-shaped stub.

The new files are independently written GPL-2.0-only project code. Actual
Wine, ReactOS, and KernelEx bodies were read before implementation. Their
algorithms, architecture differences and dependencies are described below.

## Complete family inventory and integration

| KERNEL32 export | NTDLL counterpart | Shared implementation | x86 ABI |
| --- | --- | --- | --- |
| `InitializeSListHead` | `RtlInitializeSListHead` | `m98_InitializeSListHead` | stdcall, 4 bytes |
| `InterlockedFlushSList` | `RtlInterlockedFlushSList` | `m98_InterlockedFlushSList` | stdcall, 4 bytes |
| `InterlockedPopEntrySList` | `RtlInterlockedPopEntrySList` | `m98_InterlockedPopEntrySList` | stdcall, 4 bytes |
| `InterlockedPushEntrySList` | `RtlInterlockedPushEntrySList` | `m98_InterlockedPushEntrySList` | stdcall, 8 bytes |
| `InterlockedPushListSList` | `RtlInterlockedPushListSList` | `m98_InterlockedPushListSList` | **fastcall**, ECX/EDX plus 8 stack bytes |
| `InterlockedPushListSListEx` | `RtlInterlockedPushListSListEx` | `m98_InterlockedPushListSListEx` | **stdcall**, 16 stack bytes |
| `QueryDepthSList` | `RtlQueryDepthSList` | `m98_QueryDepthSList` | stdcall, 4 bytes; USHORT result |
| — | `RtlFirstEntrySList` | `m98_RtlFirstEntrySList` | stdcall, 4 bytes |

`src/m98_slist.h` declares these bodies and the seven Rtl aliases. Compile
`src/m98_slist.c` once into each provider that exposes this family. The isolated
`tests/slist_fixture.c` shows complete, alphabetically sorted KERNEL32/NTDLL
tables. KERNEL32 integration requires all seven entries so list state is never
split between this implementation and KernelEx's old partial implementation.
NTDLL binding and KERNELBASE forwarding require their own integration evidence.
Modern SDKs may macro-map `InterlockedPushListSList` to the Ex spelling; export
binding must retain the actual legacy fastcall entry rather than that macro.

The current `sync-slist` catalogue has 28 rows: seven KERNEL32 exports, seven
KERNELBASE forwarding candidates, eight NTDLL routines, and six NTDLL `Exp*`
fault/resume/end labels. The `ExpInterlockedPopEntrySList{Fault,Resume,End}` and
`...{Fault8,Resume8,End8}` names are architecture-specific **instruction labels**,
not ordinary callable functions. This module's private labels serve its scoped
SEH implementation; they are not drop-in NT kernel trap-handler addresses.
The catalogue's 28 rows must not be marked complete merely because the fifteen
user routines have common bodies. The fixture exports only `get_api_table`.

## ABI, CPU and original Win98 dependencies

Build assertions verify the x86 header size and offsets: `Next` at byte 0,
`Depth` at byte 4, `Sequence` at byte 6. A link is one four-byte pointer, but
caller allocations and headers must satisfy the eight-byte x86 memory alignment
contract. Initialization is not concurrent with other operations. The header
and list entries are opaque to callers; mixing ordinary list writes with atomic
operations, inserting an already-linked node, or freeing the live header is
invalid use. See Microsoft's [initialization contract](https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-initializeslisthead)
and [batch insertion contract](https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedpushlistslistex).

The production fixture has **zero OS imports**. `LOCK CMPXCHG8B` performs atomic
header reads and compare/exchange with a full memory barrier; the compiler also
receives a memory clobber. Win98's captured original KERNEL32 export manifest
does **not** contain `InterlockedCompareExchange64`, so importing it would create
a loader failure. The PE checker verifies emitted locked CMPXCHG8B instructions
and prohibits non-native imports in the test support code. `FS:[0]` supplies
ordinary x86 Windows SEH registration without an NTDLL runtime import. No SSE,
AVX, kernel SMP service, `RtlUnwind`, or vectored-exception API is needed by the
production implementation.

CMPXCHG8B requires a Pentium-class or newer execution CPU. This is compatible
with the project's modern accelerated backend requirement. It is **not** proof
that this provider can execute on an actual i386/i486 or while a CPU-emulation
layer rejects CMPXCHG8B. Per-app historical CPU presentation and instruction
emulation are separate work. Host parallel tests likewise do not establish
Win98 kernel SMP or ShizukuDOS multicore support.

## Source analysis and choices

Pinned tarballs are checked by `build/api-sources/receipt.json`:

- Wine commit `df15af3652511150490934682202d45af892f887`, archive SHA256
  `b5cce1f99a10978ebe4a1cae1602689db77e73af9701a10a00f8be2cfb4f21ba`.
  [`dlls/ntdll/sync.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/ntdll/sync.c)
  contains all eight Rtl SList bodies. Its 32-bit branch uses pointer/depth/
  sequence CAS64, preserves the chain returned by Flush, increments sequence on
  every successful mutation, and explicitly protects Pop's node dereference
  because another thread may have freed the node. The fastcall wrapper and
  stdcall Ex entry are distinct. Its 64-bit 16-byte header and CAS128 branch are
  not applicable to this x86 module. The reviewed file is LGPL-2.1-or-later.
- ReactOS commit `9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8`, archive SHA256
  `ed7c5b41f5961e8b423477e434b48cc62ca25b01d695c716217ec494a95e702c`.
  [`sdk/lib/rtl/slist.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/sdk/lib/rtl/slist.c)
  supplies generic initialization/query/first/batch bodies, including a
  fastcall batch path. Its generic Pop is **not the actual x86 implementation**.
  [`sdk/lib/rtl/i386/interlck.S`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/sdk/lib/rtl/i386/interlck.S)
  supplies x86 Push/Pop/Flush. Push adds `0x10001` to the depth/sequence dword,
  Pop subtracts one, and Flush zeros only depth. Consequently depth carry and
  borrow affect sequence differently from Wine. It exposes exact fault/resume/
  end instruction labels. These files belong to the GPL ReactOS source tree;
  no assembly body was copied verbatim into the module.
- ReactOS
  [`ntoskrnl/ke/i386/traphdlr.c`, `KiCheckForSListFault`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/ntoskrnl/ke/i386/traphdlr.c)
  explains and implements reclaimed-node retry. It compares **both** pointer
  and depth/sequence; checking the pointer alone misses reuse at the same
  address. It restarts before ordinary page-fault processing to avoid paging or
  guard-page side effects. Its kernel trap frame and hard-coded NTDLL labels
  cannot be imported into Win98 through a DLL-name wrapper.
- KernelEx commit `31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`,
  [`apilibs/kexbases/Kernel32/inter.c`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/Kernel32/inter.c),
  has a Flush stub, non-atomic Pop, pointer-only Push CAS, and QueryDepth reading
  the low pointer bits instead of byte-offset 4. It cannot share mutation
  responsibility with a correct eight-byte sequenced-list implementation.

The host's native 32-bit WOW64 probe showed a third internal sequence policy:
sequence stayed unchanged for the sampled Push/Pop/Flush/batch calls. Those
private bits are not a public compatibility assertion. This port uses Wine's
consistent per-mutation sequence increment, with **independent** modulo-65536
depth and sequence. Public return values, list order, call ABI, depth wrapping
and concurrent ownership are compared against native APIs.

## Reclamation, ABA and exception boundaries

1. A popper atomically snapshots pointer, depth and sequence.
2. A competing thread can pop and free that node before the first popper reads
   its `Next` link. Reading a freed page can fault even though the caller used
   atomic list APIs correctly.
3. A stack-local SEH record protects only the exact `mov ebx,[eax]` instruction.
   It accepts a read access violation or in-page error at that instruction and
   the captured address. If either header word changed, it restarts from a new
   atomic snapshot. If the current header is unchanged, it propagates the real
   invalid-pointer fault. Unwind, unrelated exceptions, writes and other
   instruction addresses are not consumed.
4. If the old memory remains readable or is reallocated, compare/exchange still
   rejects the stale snapshot when its sequence differs. The caller owns an
   entry only after successful removal. `RtlFirstEntrySList` returns a snapshot
   and provides **no lifetime pin** for dereferencing that entry.

The fixed x86 header has only sixteen sequence bits. After 65,536 mutations,
the sequence wraps; pointer, depth and sequence can all equal an old snapshot.
An indefinitely suspended popper combined with that full ABA recurrence is not
made safe by CMPXCHG8B or SEH. This is a real finite-tag limitation of this
algorithm, not a claim of unlimited safe reclamation. Applications needing an
unbounded reclamation guarantee require an epoch/hazard protocol or another
ownership scheme; adding such a protocol changes integration and cannot be
silently imposed on external SList callers. The tests exercise wrap and heavy
reuse, but do not prove that adversarial full-tag ABA is impossible.

Win98 user SEH also cannot reproduce ReactOS's pre-page-fault kernel redirect:
guard-page and paging side effects may already have occurred before dispatch.
Guard-page exceptions are deliberately not swallowed. This implementation
claims the tested AV/in-page retry scope, not full NT trap-handler equivalence.
Misaligned pointers and invalid chains have no invented success or recovery
contract. They retain the documented invalid-use boundary.

## Tests and current evidence

Run `tools/build-slist.ps1` to build with the repository's CRT-free i686 compiler
and run the isolated host comparison. `-SkipHostExecution` builds/checks only.

| Artifact in `build/slist` | Purpose |
| --- | --- |
| `SLISTFIX.DLL` + `slist_smoke.exe` | Direct API table with all 15 KERNEL32/Rtl names; Rtl aliases must resolve to the same bodies |
| `slist_import_probe.exe` | All seven real KERNEL32 imports, including fastcall legacy batch and stdcall Ex; runs natively on the host and through a provider in Win98 |
| `slist_integrated_probe.exe` | Loads the seven KERNEL32 mappings from `M98WRAP.DLL`; separate integration gate |
| `slist_fault_smoke.exe` | Module linked directly with a compile-only test hook for deterministic reclaim fault injection, plus real concurrent free stress |
| `slist_native_probe.exe` | Diagnostic native x86 header transitions, including deliberately synthesized depth/sequence values; not a guest compatibility gate |

Host results on 2026-09-24:

- Production fixture and native static imports both passed alignment, empty and
  LIFO behavior, LastError preservation, prior-head returns, 12,000 fastcall/
  stdcall batch cycles, complete Flush chains, a 65,537-node chain, and depth
  wrap through zero while the list remained nonempty.
  The large-chain test uses VirtualAlloc for page alignment; a separate HeapAlloc
  diagnostic distinguishes legacy heap size failure from insufficient alignment.
  A Win98 run of the initial test reached the large HeapAlloc assertion after
  the basic batch/LIFO tests passed, so the original combined allocation and
  alignment assertion was replaced with these separate diagnostics.
- Both passed simultaneous four-producer/four-consumer exchange of 40,000 items
  through 128 repeatedly reused nodes, checking unique ownership, payload
  publication, exact consumption and return of every node.
- The direct module fault probe passed forced other-thread free/retry, forced
  same-address/new-sequence retry, propagation of an unchanged invalid pointer
  to the caller's SEH, exact 16-bit sequence wrap after 65,536 mutations, and
  four-thread allocation/push/pop/free of 6,000 real pages.
- `tests/check_slist_pe98.py` passed PE32 x86/subsystem 4.10, allowed native
  imports, no TLS/delay/CLR or ASLR/NX/NO_SEH flags, locked CMPXCHG8B presence,
  and no leaked internal/test exports. The production fixture has zero imports.

The fault-test hook is absent from the production DLL. The tests' allocations,
events, threads, VirtualProtect/VirtualFree and file-output functions are checked
against `benchmarks/win98se-ko-oem-native-exports-v1.json`. That manifest is an
original-media export-name receipt; emitted CPU instructions and SEH runtime
behavior require the additional checks above and guest execution respectively.

## Installed Win98 guest results

The directly installed Win98 SE guest passed both the isolated fixture and the
production provider17 direct-table probe, including the 65,537-node chain and
four-producer/four-consumer 40,000-transfer test. Both returned exit code zero,
with output SHA256
`97e24e9814387fd0b3a4aff10de1a705cebf85d5e7ac12f26871eb766caf7e02`.
Local receipts are `build/guest/receipt-slist-direct-final.json` and
`build/guest/receipt-slist-integrated17.json`. The corrected allocation diagnostic
showed a successful native HeapAlloc pointer at offset **4 modulo 8**. The
failure in the initial probe was therefore insufficient alignment on this
guest, not failure of SList to support a large chain. Page-aligned VirtualAlloc
storage allowed the complete test to pass.

The latest self-contained fault probe, executable SHA256
`76cda95be4731a82beb241297d6ae056b4d1129ae9054ba62469ba48b1100c71`, also
returned exit code zero in Win98. It passed reclaimed-page restart, same-address
sequence restart, unchanged invalid-pointer propagation, 65,536-mutation
sequence wrap and 6,000 concurrent page-free cycles. Output SHA256:
`2dc8332fe703e0daae4a2d4655604f348fa251e2719010bf5ac1700968f57341`;
local receipt: `build/guest/receipt-slist-fault-final.json`.

These results establish the exercised Win98 behavior and production direct-table
binding. Static guest imports, NTDLL/KERNELBASE routing and application startup
remain separate integration gates. No app-compatibility percentage or full
`sync-slist` catalogue completion is inferred from these passes.

## Production static import checkpoint

After cold boot with `M98WRP18.DLL` (`0da854a1a6ff42296e2e5c7fda92e07a660351a5b14b568f18c2846c609c2f3e`), the seven-name KERNEL32 static-import suite passed in the installed Win98 SE guest, including depth wrap and 40,000 concurrent transfers. Output SHA-256: `97e24e9814387fd0b3a4aff10de1a705cebf85d5e7ac12f26871eb766caf7e02`. Full receipt and source/test hashes are in `benchmarks/api-guest-evidence-v1.json`. This does not install the NTDLL aliases.
