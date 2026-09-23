# VxKex source audit for the Windows 98 lab

Audit date: 2026-09-24. This is a **source review**, not a VxKex build, a
Windows 98 guest test, or a claim that its binaries run on Windows 98.

## Source identity and rights

- Inspected the public [i486/VxKex source tree](https://github.com/i486/VxKex)
  at [`735bad15b1ea1f55dafcde9e7d0d12ba61fae230`](https://github.com/i486/VxKex/tree/735bad15b1ea1f55dafcde9e7d0d12ba61fae230),
  the `main` HEAD returned by `git ls-remote` on the audit date. Its README
  identifies VxKex as Windows 7 API extensions. The repository maintainer
  describes it as carrying source from original author `vxiiduu` in
  [issue #41](https://github.com/i486/VxKex/issues/41). Other VxKex forks are
  separate trees and were not used as this audit's pinned reference. The former
  `github.com/vxiiduu/VxKex` Git URL did not resolve on the audit date, so
  `i486/VxKex` is described here as a maintained public source mirror rather
  than an author-owned upstream.
- The pinned tree has **no tracked `LICENSE`, `LICENCE`, `COPYING`, or `NOTICE`
  file** (`git ls-tree -r --name-only HEAD`), and the
  [GitHub repository metadata](https://api.github.com/repos/i486/VxKex)
  reports `license: null`. Sampled source headers give author and revision
  history, but no reuse grant. The README's offer of source code is not an
  explicit software license. A release archive might have additional terms;
  those were not established in this audit.
- Therefore this project records VxKex as a **reference only**. It does not
  copy VxKex source, headers, tables, import libraries, or binaries, and does
  not add a VxKex submodule to the public repository. Any later code import
  needs a specific grant and per-file provenance review. New Win98 code may
  implement public API contracts independently under this project's license.

## What its loader actually does

The [README](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/README.md)
describes per-program enabling through Image File Execution Options (IFEO)
`VerifierDlls`, followed by import-table rewriting. In
[`KexDll/dllmain.c`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/KexDll/dllmain.c),
the verifier DLL receives `DLL_PROCESS_VERIFIER`, reads the NT TEB/PEB,
registers `LdrRegisterDllNotification`, and rewrites the main image's import
directory. [`KexDll/dllrewrt.c`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/KexDll/dllrewrt.c)
also handles rewrites for loaded modules and can change mapped image protection
with `NtProtectVirtualMemory` before editing an import name. The
[`redirects.h` table](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/KexDll/redirects.h)
maps `kernel32` and `kernelbase` to `kxbase`, `ntdll` to `kxnt`, and named
`api-ms-win-*` families to extension DLLs. Its
[`apiset.c`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/KexDll/apiset.c)
queries whether such a rewrite entry exists.

The extension DLLs are built for a Windows 7 baseline. For example,
[`KxBase/forwards.c`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/01-Extended%20DLLs/KxBase/forwards.c)
forwards `GetTimeFormatEx` and `GetTickCount64` to Windows 7 `kernel32`;
these are **not** Windows 98 implementations. The pinned
[`Build VxKex.bat`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/Build%20VxKex.bat)
requires the Windows 7.1 SDK and MSBuild for x86 and x64 configurations.

The pinned source tree contains no dedicated `UXTHEME` source file or
`uxtheme` redirect in `KexDll/redirects.h`. Its `KxBase` and `KxUser`
`buildcfg.h` files define `_UXTHEME_H_` only to exclude a header during their
build. VxKex consequently supplies no identified drop-in answer to this
project's `DrawThemeTextEx` or `GetThemeSysFont` work.

## Windows 98 portability boundary

- Windows 98 has no NT TEB/PEB, `ntdll` loader, Application Verifier
  `DLL_PROCESS_VERIFIER` path, or `LdrRegisterDllNotification` contract used
  by this loader. The IFEO injection and mapped-image rewrite mechanism must
  be redesigned around the existing KernelEx resolver, a separately verified
  Win9x loader integration, or a reversible host-side PE transformation.
- A forwarded export is only usable if the destination exists in the original
  Windows 98 OEM DLL or a tested project wrapper. Forwarding a modern name to
  Windows 7 `kernel32`/`ntdll` does not create its behavior in Windows 98.
  The project already has an independent `GetTimeFormatEx` candidate in
  `src/m98wrap.c`; VxKex's `forwards.c` supplies no implementation to port.
- [`KxBase/time.c`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/01-Extended%20DLLs/KxBase/time.c)
  calls `RtlGetSystemTimePrecise`, and
  [`KxBase/thread.c`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/01-Extended%20DLLs/KxBase/thread.c)
  reads the NT TEB or uses `NtQueryInformationThread(ThreadNameInformation)`.
  Their behavior needs new Win98 storage and clock paths; direct linking fails.
- [`KexDll/rtlwoa.c`](https://github.com/i486/VxKex/blob/735bad15b1ea1f55dafcde9e7d0d12ba61fae230/KexDll/rtlwoa.c)
  coordinates `WaitOnAddress` with NT SRW locks and keyed events
  (`NtWaitForKeyedEvent`/`NtReleaseKeyedEvent`). Windows 98 needs its own
  synchronization primitives and race tests before an equivalent can be
  claimed. Its x64 build also does not provide a native x64 execution path for
  the 32-bit Windows 98 guest.

## Candidate work informed by the audit

1. **API-set import triage.** Independently parse `api-ms-win-*` and
   `ext-ms-win-*` import names in the host inventory and map each requested
   export only when a Windows 98 OEM or tested KernelEx/project implementation
   exists. Use the VxKex redirect organization as a coverage checklist, not as
   an executable table or a reason to count unimplemented APIs.
2. **Per-application import mapping.** Specify an app profile that resolves
   static and dynamic DLL requests through KernelEx or an offline, backed-up
   PE import transformation. A first prototype should prove that it preserves
   PE layout, bound-import handling, loader behavior, and rollback on a
   disposable guest before it is installed more broadly. Do not transplant
   VxKex's NT verifier injection.
3. **Address waiting.** When measured target imports require
   `WaitOnAddress`/`WakeByAddressSingle`/`WakeByAddressAll`, implement them
   independently using Win98-capable waits. Test unequal-value immediate
   return, 1/2/4/8-byte values, timeout, single/all wake, lost-wake races,
   and multiple producers before counting compatibility. The VxKex source
   identifies the necessary race cases but depends on NT keyed events.
4. **Thread descriptions.** A project-owned per-process map keyed by verified
   Win98 thread handles could expose `SetThreadDescription` and
   `GetThreadDescription`, with documented lifetime and `LocalFree` ownership.
   Do not call VxKex's NT `ThreadNameInformation` path.

No code or runtime behavior is attributed to VxKex in this project yet.
The current guest evidence for UXTHEME and time formatting remains the
independent KernelEx/Wine/ReactOS-based work recorded elsewhere in `docs/`.
