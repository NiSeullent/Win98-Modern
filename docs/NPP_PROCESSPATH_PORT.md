# QueryFullProcessImageNameA/W Windows 98 port

## Scope and contract

After the application-restart imports were installed and tested, the directly
installed Windows 98 SE guest reported `KERNEL32.DLL!QueryFullProcessImageNameW`
as the next Notepad++ 8.9.8 loader dependency. This change adds both the A and
W four-argument `WINAPI` entries to the sorted KernelEx KERNEL32 table.

For the current process, a valid pseudo-handle returns the full drive-qualified
DOS image path. A real handle to the current process also works if KernelEx's
`GetProcessId` API can be resolved at run time. The wrapper first checks the
process handle with native `GetExitCodeProcess`, compares a real handle's PID
with `GetCurrentProcessId`, then reads `GetModuleFileNameA`. It expands long
path components when `GetLongPathNameA` can resolve the saved path and converts
the W result through the installed ANSI code page. If long-name expansion is
unavailable because the image has been renamed, the original complete module
path is retained. Both variants return a NUL-terminated path and update
`lpdwSize` to the number of characters **excluding** the NUL.

The caller's buffer and size stay unchanged on `ERROR_INSUFFICIENT_BUFFER`,
including when capacity equals the returned path length. A zero capacity
fails with `ERROR_INSUFFICIENT_BUFFER`. A null output pointer with otherwise
sufficient capacity or a null size pointer fails with
`ERROR_INVALID_PARAMETER`. Invalid flag bits fail with
`ERROR_INVALID_PARAMETER`; `PROCESS_NAME_NATIVE` (1) fails with
`ERROR_NOT_SUPPORTED` because Windows 98 has no NT `\Device\...` namespace.
Invalid process handles retain the native `GetExitCodeProcess` error. Other
processes fail with `ERROR_NOT_SUPPORTED` rather than returning the caller's
image path. A valid real self handle on a configuration where `GetProcessId`
cannot be resolved also fails explicitly.

This is a **bounded current-process implementation**, not a general process
image query. For other processes, Win98's native KERNEL32 does not expose the
NT `ProcessImageFileName` query used by Wine and ReactOS. A future broader port
would need a verified handle-to-PID path plus a reliable main-image lookup. A
Toolhelp module enumeration alone cannot safely treat an arbitrary first
module or matching basename as the process image. This symbol's presence is
not evidence that Notepad++ starts.

## Pinned source review and license

- [Microsoft's A](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamea) and [W](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew) contracts define the process query rights, flag values, NUL termination, and returned length. The pinned Wine [kernel32 process tests](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernel32/tests/process.c#L2004-L2153) explicitly cover the unchanged size and buffer on insufficient capacity.
- Pinned Wine `df15af365251` [kernelbase/debug.c](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/debug.c#L1730-L1821) obtains the process image through an NT query and converts the Win32 path to a native device path when requested.
- Pinned ReactOS `9dc3ca87209f` [kernel32_vista/vista.c](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32_vista/vista.c#L24-L129) selects `ProcessImageFileName` or `ProcessImageFileNameWin32` through `NtQueryInformationProcess`. Those NT facilities are absent from Windows 98.
- Pinned KernelEx `31cdfc3560fc` [process.c](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/Kernel32/process.c#L381-L394) publishes a Win98 `GetProcessId` implementation. This wrapper looks up that export only for a real process handle; `GetProcessId` is not a load-time import. The wrapper's own API table does not contain `GetProcessId` or `GetProcAddress`, avoiding a self-routing loop.

No Wine, ReactOS, or KernelEx implementation code was copied into this port.
The new code is original project code under GPL-2.0-only. The four new
load-time imports (`GetCurrentProcess`, `GetCurrentProcessId`,
`GetModuleHandleA`, and `GetProcAddress`) are present in the pinned Korean
Windows 98 SE OEM KERNEL32 export manifest. The wrapper still resolves only
native Windows 98 DLL imports at load time.

## Verification state

Run `./tools/build-processpath.ps1` from the repository root. It builds a
timestamp-free Win98 PE32/i386 DLL, a direct table smoke test, a static KERNEL32
import probe, and the full API-table smoke. `tests/check_processpath_pe98.py`
compares DLL imports against the original-media manifest, checks Win98 loader
settings, and requires both new static imports in the probe.

The direct smoke exercises A/W path content, returned lengths, real self
handles, buffer and flag boundaries, invalid handles, and explicit rejection
of another process even when that process is a second copy of the same EXE.
The host PE gate, direct test, static import probe, and 48-entry API-table
smoke passed. On 2026-09-24 the exact `build/processpath/m98wrap.dll` SHA-256
`4483ddd6e393d9877398618e12844e226f364789fce35e82ea6de921bf6ac55e`
passed the direct Windows 98 guest probe as `C:\M98LAB\M98WRAP.DLL`:
`PASS: bounded process image path A/W contract` (exit code 0). The DLL was
then installed as versioned `C:\WINDOWS\KernelEx\M98WRP10.DLL`, with the
byte-preserving `CORE.INI` contents update SHA-256
`e9f6c78ebb764c5710b0761ab79efbed90d135b426e702198c85f3a5b368095b`.
After a normal shutdown and a WHPX/NEM-accelerated cold boot, the static
`KERNEL32` import probe and 48-entry sampled smoke both passed with exit code
0. This includes the tested dynamic `GetProcessId` route for a real self
handle; it is not a cross-process image lookup or full API contract claim.

The next Notepad++ 8.9.8 x86 launch still failed before startup. The loader
dialog named `KERNEL32.DLL!SleepConditionVariableSRW`
(`vm/npp-after-processpath.png`). No required modern app has been shown to
run from this port.
