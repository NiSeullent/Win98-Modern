# 0.1.7-preview: GDI32 drawing aliases and source audit

This is an experimental patch for an existing, user-installed Windows 98 SE
guest with KernelEx 4.5.2. It is not a Windows 98 image, a complete operating
system build, or evidence that the selected applications function.

## Shipped change

`M98GDI.DLL` supplies `GDI32.GdiAlphaBlend`, `GdiTransparentBlt` and
`GdiGradientFill` through the KernelEx provider table. The install procedure
places it as `M98GDI3.DLL` in the KernelEx directory and routes the three names
in each active profile. It checks the adjacent, independently installed
KernelEx `MSIMG32.DLL` before calling it. The tested auxiliary binary's
SHA-256 is `e2a644c38cd814a63239b0376cb9921d824c492b8816781ae7f3e8724efabee5`;
it is not redistributed in this release. The original Windows 98 OEM
`MSIMG32.DLL` changed pixels outside the tested rectangles and is excluded
as a backend. The adapter rejects oversized raster requests, unsupported
blend modes and invalid gradient mesh indices. See
[`GDI_ALPHA_PORT.md`](GDI_ALPHA_PORT.md) for contracts and remaining limits.

The shipping `M98GDI.DLL` SHA-256 is
`d61826f03a21e04ba5a983ee99db8a9cfff3ce33aee7832e10086f7581bc5ac2`.
An independent review found and corrected a rectangle gradient work-budget
error before this build was installed. Its host test covers the exact
1024×1024 pixel boundary and rejects 1024×1025 in both rectangle modes.

## Direct Windows 98 evidence

The directly installed Korean Windows 98 SE guest was cold-booted under
VirtualBox's hardware-accelerated NEM backend after the CORE routes moved
from `m98gdi2` to `m98gdi3`. The corrected provider passed 14 bounded direct
pixel/error cases, 14 cases through static `GDI32.DLL` imports, and an
independent three-name import probe. A 16-cycle load/call/unload test passed
for the earlier TLS-fixed provider and the corrected source passed the same
host-side lifetime test. The installed USER32 clipboard regression suite
passed 6/6 after the final GDI route migration. The static contract receipt,
provider and source hashes are recorded in
[`gdi-alpha-v3-evidence.json`](../benchmarks/gdi-alpha-v3-evidence.json).
These are specific behavior checks, not a percentage of GDI32 or all Win32.

Notepad++ 8.9.8 x86 now passes its earlier `GDI32.GdiAlphaBlend` loader
failure, but its writable-folder run displays `Exception On WM_CREATE` /
`Access violation` and never shows a working editor. A bounded debug trace
points strongly to an unimplemented `COMCTL32` ordinal 381 call in the tab
icon path; the runtime IAT value was not captured. See
[`NPP_WM_CREATE_CRASH_TRIAGE.md`](NPP_WM_CREATE_CRASH_TRIAGE.md) and
[`npp-seh-wm-create-v1.json`](../benchmarks/npp-seh-wm-create-v1.json).

## Rebuild and archive checks

The public `win98-modern-0.1.7-preview-x86.zip` SHA-256 is
`8ea6f1d092956d7c0f0f61a83965f5731ca9bbb891323d5ef8847e4f6e2043e4`.
Its 205 entries include 204 checksummed files plus `SHA256SUMS.txt`. The
source-only archive was extracted to an isolated folder and rebuilt without
the repository checkout. Its GDI DLL was byte-identical to the packaged,
guest-tested binary. The archive excludes Windows installation media, product
keys, the installed KernelEx auxiliary backend, and the selected app binaries.
Follow [`INSTALL-ko.md`](../release/INSTALL-ko.md) for backup, versioned
installation, cold boot and rollback.

The full Windows API target, five-app functional target, ShizukuDOS as a
Windows 98 boot replacement, OS multicore, PAE integration, ShizukuFS driver
and modern storage/USB drivers remain open work. The catalogue snapshot is
an auditable work queue rather than a compatibility score.
