# UXTHEME no-theme bridge and KernelEx KnownDLL test

The pinned 32-bit Notepad++ 8.9.8 executable imports 16 symbols from
`UXTHEME.DLL`. The KernelEx 4.5.2 `auxiliary/uxtheme` DLL supplies classic
no-theme responses for older functions, but does not export
`DrawThemeTextEx`, `BeginBufferedAnimation`, `BufferedPaintRenderAnimation`,
`BufferedPaintStopAllAnimations`, `EndBufferedAnimation`, or
`GetThemeTransitionDuration`. The guest loader therefore stopped at
`UXTHEME.DLL!DrawThemeTextEx` after the shell bridge was activated.

`src/uxtheme_shim.c` and `src/uxtheme_shim.def` provide a no-theme API bridge.
Build with `tools/build-uxtheme.ps1`. The unique filename
`m98uxtheme.dll` allows direct host and guest tests without shadowing an
installed system library. Copying it beside `notepad++.exe` as `UXTHEME.DLL`
was tested, but **did not satisfy the app loader** in the installed KernelEx
setup; do not treat that placement as a working integration instruction.
KernelEx's installer registers `UXTHEME` in
`HKLM\Software\KernelEx\KnownDLLs`, and its pinned `core/resolver.cpp`
redirects a bare `UXTHEME.DLL` import to its own installed DLL. That DLL lacks
the newer `DrawThemeTextEx` export. The exact same loader error recurred after
the app-local copy was present and SHA-256 readable at `D:\NPP\UXTHEME.DLL`.
An export-preserving KernelEx KnownDLL replacement is built from the pinned
KernelEx UXTHEME sources and this bridge. The guarded test helper
`known_dll_switch.exe` accepts only the original `UXTHEME.DLL` and candidate
`UXTNEW.DLL` registry values. The disposable VM retains the original file,
has a snapshot named `before-uxtheme-known-20260923`, and can restore the
mapping with `KSWITCH.EXE restore` followed by a restart. The candidate
filename matters: KernelEx maps the registry value to a filename inside its
own directory. This changes the test VM's global UXTHEME mapping, so it is
not part of the public installation instructions yet.

`tools/build-uxtheme-known.ps1` verifies the pinned KernelEx Git commit and
combines its original `uxtheme.c` and `metric.c` with the new functions. The
combined DLL retains **all 48 original export names**, maps ten overlapping
Notepad++ names to the tested no-theme implementations, and adds six missing
names. Pinned KernelEx metric functions remain available. The PE gate checks
all 54 exported names, the exact Notepad++ imports, Win98 native imports, and
base relocations. The original KernelEx `metric.c` is LGPL-2.1-or-later;
distribution of the combined binary must include the matching source and
license notices. Several original KernelEx exports still report `E_NOTIMPL`.

The Windows 98 desktop has no Windows XP/Vista theme service. This bridge
reports `IsThemeActive == FALSE`, `OpenThemeData == NULL`, and `E_HANDLE` for
operations on a theme handle. It does not claim to render visual styles.
`DrawThemeParentBackground` paints by sending `WM_ERASEBKGND` and
`WM_PRINTCLIENT` to the actual parent window with adjusted DC origin and
clipping. Buffered animation creation returns `NULL` and clears the output
DCs, which tells the caller to use its normal drawing path; no fake animation
handle is exposed. `SetWindowTheme` and enabling theme textures report
`E_NOTIMPL` when they would require a theme service. These are explicit
limits and are not counted as full themed API behavior.

## Source analysis and license

- Pinned [Wine `dlls/uxtheme/draw.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/uxtheme/draw.c) rejects a missing theme handle with `E_HANDLE` in `DrawThemeTextEx`, and its parent-background function sends the parent paint messages. Pinned [Wine `dlls/uxtheme/buffer.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/uxtheme/buffer.c) returns a null animation handle when animation is unavailable.
- Pinned [ReactOS `dll/win32/uxtheme/draw.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/uxtheme/draw.c) has the same invalid-theme result and parent painting path. Its [buffer implementation](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/uxtheme/buffer.c) likewise has no working animation engine. Their LGPL-2.1-or-later code was **consulted, not copied**.
- Microsoft's [DrawThemeTextEx](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-drawthemetextex) contract requires a theme handle and reports HRESULT failure. The [BeginBufferedAnimation](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-beginbufferedanimation) documentation explicitly directs callers to ordinary painting when its return is null.
- The new bridge is GPL-2.0-only project code. Its compile-time Vista declarations do not change the emitted PE subsystem: 4.10, PE32 i386. Every native DLL import was checked against `benchmarks/win98se-ko-oem-native-exports-v1.json`.

## Verification to date

- `tools/build-uxtheme.ps1`: host build and PE loader gate pass. `tests/check_uxtheme_pe98.py` compares the DLL's export names with the exact pinned Notepad++ import descriptor, checks relocation and forbidden directories, and requires all native imports to exist in the original Windows 98 SE OEM exports.
- `build/uxtheme_smoke.exe`: host and Windows 98 guest PASS for exact exports, missing-theme error results and untouched outputs, animation fallback, and parent-window painting. The guest uses 16-color VGA, so the smoke checks that parent messages changed a black-initialized pixel without requiring an exact modern RGB value. Its static imports are present in the OEM manifest.
- `vm/npp-after-uxtheme.png`: app-local DLL on the refreshed test CD was present and SHA-256 verified by the remote agent, but Notepad++ still failed at `UXTHEME.DLL!DrawThemeTextEx` because of the KnownDLL redirection.
- The combined DLL (`build/uxtheme-known/UXTHEME.DLL`, guest SHA-256 `5690f843b3df1773693afd122b0a5d50893c40249890f83d6a9109f31ac0a842`) was SHA-verified at `C:\WINDOWS\KernelEx\UXTNEW.DLL`. `KSWITCH.EXE inspect` read the original value, `switch` changed it to `UXTNEW.DLL` with readback, and the value survived a normal GUI restart. `vm/npp-after-known-theme.png` then showed the **next** loader error, `KERNEL32.DLL!GetTimeFormatEx`, rather than `DrawThemeTextEx`. This proves the candidate satisfied the app's static UXTHEME imports in this guest. It does not prove those functions are all called correctly at runtime or that Notepad++ starts.
