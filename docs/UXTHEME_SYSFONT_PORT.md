# Win98 system-font UXTHEME repair

The pinned KernelEx `auxiliary/uxtheme/metric.c` implementation passes `&plf`
to `SystemParametersInfoW` for `TMT_ICONTITLEFONT`. That address is the local
pointer variable, not the caller's `LOGFONTW` buffer. The call can overwrite
the function's stack and does not fill the requested font. The other five IDs
use a `NONCLIENTMETRICSW` query, but Windows 98's native `USER32` reliably
provides its desktop configuration through the ANSI entry point.

`src/uxtheme_sysfont.c` replaces this **one** export in the combined KernelEx
KnownDLL. It calls native `SystemParametersInfoA` for all six documented font
IDs, copies the numeric fields from `LOGFONTA`, and converts the face name from
the current ANSI code page to UTF-16 with native `MultiByteToWideChar`. A NULL
theme handle reads global system metrics as specified by
[Microsoft's GetThemeSysFont contract](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-getthemesysfont).
The installed no-theme bridge cannot create a valid theme handle, so it returns
`E_HANDLE` for a non-NULL one. NULL output returns `E_POINTER`; unknown font IDs
return `STG_E_INVALIDPARAMETER`. All failures leave caller storage unchanged.

The algorithm was independently written after reviewing pinned
[Wine metric.c](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/uxtheme/metric.c)
and [ReactOS metric.c](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/uxtheme/metric.c).
Neither implementation was copied. This file is project GPL-2.0-only source.
The combined binary still includes pinned KernelEx LGPL-2.1-or-later object
code and requires the existing corresponding source and notices for release.

Run `tools/build-uxtheme-known.ps1` to build and verify. The PE gate checks
that `GetThemeSysFont` maps to `m98_GetThemeSysFont`, retains all 54 exported
names, and imports only exports confirmed in the original Win98 OEM media.
The host smoke compares all six output fonts against live ANSI desktop metrics,
checks buffer guards, and exercises invalid inputs. On 2026-09-23 this build
passed both host smokes and the PE gate. The build omits volatile PE timestamps
for reproducibility; the resulting DLL SHA-256 was
`cd6d0e9410febd9a54b56a142c8764213801b01603810e2213a69f4fd944f50d`.
The build also produces `build/uxtheme-known/uxtheme_sysfont_guest_probe.exe`.
It has a static `UXTHEME.DLL!GetThemeSysFont` import, and its PE gate confirms
PE32 console subsystem 4.10 and original Win98 OEM dependencies. The guest
probe applies the same six-font comparison and invalid-input checks through
KernelEx KnownDLL resolution; building it does not constitute a guest pass.

On 2026-09-24 the binary above was transferred with its SHA-256 verified to
`C:\WINDOWS\KernelEx\UXTNEW2.DLL`. A guarded registry helper set KernelEx's
UXTHEME KnownDLL mapping to that versioned name. A **normal shutdown and cold
boot** reached the Windows 98 SE GUI; the static-import guest probe passed all
six ANSI-backed system-font comparisons and invalid-input checks with exit 0.
The original `UXTHEME.DLL` remains on disk. A preceding warm restart that
combined the new theme and KERNEL32 changes produced one VxD exception and
VirtualBox triple fault. The crash state is preserved in a VM snapshot, and
the later cold boot passed. Repeated warm-restart reliability remains a
separate test; this result validates the cold-booted static call path only.

On 2026-09-24, after the later `M98WRP4.DLL` date-format update, one normal
Windows 98 **Restart** from the GUI reached the GUI again with the same
`UXTNEW2.DLL` KnownDLL mapping. The COM1 guest agent restarted and answered a
ping. This is a successful warm restart of that configuration, but it does not
erase the earlier VxD fault or establish repeated restart reliability.
