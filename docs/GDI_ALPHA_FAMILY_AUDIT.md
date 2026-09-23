# GDI alpha, transparency and gradient drawing family audit

Status: **inventory and source audit only**. The directly installed Windows
98 SE guest advances past the routed USER32 clipboard imports, then fails to
load Notepad++ 8.9.8 x86 at `GDI32.DLL!GdiAlphaBlend`. The exact executable,
provider, CORE and screenshot identities are in
`benchmarks/npp-clipboard-user1.json`; its `APP_PROBE` exit 2 / Win32 error
31 is a loader failure, not an editor launch. This audit defines a coherent
three-export drawing family and its existing GDI dependencies. It does not
install or route a new GDI provider or assert native pixel correctness.

## Complete immediate family, by DLL identity

The generated catalogue `build/api-catalog/catalog.jsonl.gz` contains 701
Win32 SDK candidate names under `GDI32.DLL` and the three ordinary
`MSIMG32.DLL` drawing names below. This table selects the **entire immediate
alpha/transparent/gradient alias family**, rather than a single Notepad++
import. The OEM column is from
`benchmarks/win98se-ko-oem-native-exports-v1.json`, extracted from the
supplied Korean Windows 98 SE OEM ISO. Presence is an export observation,
not a behavioral pass.

| DLL/name | SDK catalogue | OEM Win98 export | Porting role |
| --- | --- | --- | --- |
| `GDI32!GdiAlphaBlend` | Yes | **No** | Missing 11-argument `BOOL WINAPI` alias; current loader blocker. |
| `GDI32!GdiTransparentBlt` | Yes | **No** | Missing 11-argument color-key/stretch sibling. |
| `GDI32!GdiGradientFill` | Yes | **No** | Missing 6-argument rectangle/triangle gradient sibling. |
| `MSIMG32!AlphaBlend` | Yes | **Yes**, ordinal 2 | Native candidate backing alpha alias. |
| `MSIMG32!TransparentBlt` | Yes | **Yes**, ordinal 5 | Native candidate backing color-key alias. |
| `MSIMG32!GradientFill` | Yes | **Yes**, ordinal 4 | Native candidate backing gradient alias. |
| `GDI32!EngAlphaBlend`, `EngTransparentBlt`, `EngGradientFill` | Yes | **No** | NT display-driver `SURFOBJ`/`CLIPOBJ` surface, not HDC-call aliases; separate backend work. |

The nearby OEM-native raster foundation includes `GDI32!BitBlt`,
`StretchBlt`, `MaskBlt`, `PlgBlt`, `GetStretchBltMode`,
`SetStretchBltMode`, `CreateDIBSection`, `GetDIBits`, `SetDIBits`,
`StretchDIBits`, `CreateCompatibleDC`, `CreateCompatibleBitmap`,
`SelectObject`, `DeleteObject`, `DeleteDC`, `GetObjectA`, and
`GetDeviceCaps`. These are dependencies/test substrates, not new export
implementations. `EngBitBlt`/`EngStretchBlt` and Win32U `NtGdi*` are NT
driver/syscall surfaces; adding their names to a Win98 user-mode table would
not recreate the required object model. `MaskBlt` and `PlgBlt` remain in the
larger raster family but were already exported by the OEM GDI32; they need
behavior checks if a shared software fallback uses them. No `GdiPlgBlt`
alias is silently inferred from `PlgBlt`.

## ABI and original Win98 binary evidence

[Microsoft's GdiAlphaBlend contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdialphablend)
defines 11 x86 four-byte arguments, with the four-byte `BLENDFUNCTION`
passed **by value** as the last argument. Return is `BOOL`; x86 calling
convention is `WINAPI`/stdcall (44-byte callee stack cleanup). The
corresponding [AlphaBlend contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-alphablend)
has the same HDC/coordinate/size/blend layout but belongs to `Msimg32.dll`.
[GdiTransparentBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gditransparentblt)
and [TransparentBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-transparentblt)
each take 11 four-byte arguments including the final color key;
[GdiGradientFill](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdigradientfill)
and [GradientFill](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gradientfill)
each take 6 four-byte arguments. A `BLENDFUNCTION *` adapter, C default
calling convention or wrong stack size would corrupt the guest caller.

The OEM `WIN98_33.CAB` member `msimg32.dll` was independently extracted for
**read-only PE inspection** into ignored `build/gdi-alpha-audit/`. Its SHA-256
`6a099207614102bf2e98a0055e6d06e3ad6cae8ed4e3c648f2ca08e996a42442`
matches the OEM manifest's module receipt; size is 53,248 bytes. PE exports
`AlphaBlend`/2, `GradientFill`/4 and `TransparentBlt`/5 each have a code RVA,
not a PE forwarder. Its named imports from `GDI32`, `USER32`, `KERNEL32`,
`MSVCRT` and `NTDLL` are all present in the same OEM native-export manifest.
This establishes a plausible Win98-native call path **for that exact CAB
member**. It does not prove the installed guest uses the same DLL or that
the functions match modern edge-case/pixel semantics. A different
`MSIMG32.DLL`, including KernelEx's auxiliary version, must be identified
by path and hash before choosing the backend.

## Pinned implementation analysis and license boundary

- Wine `df15af3652511150490934682202d45af892f887`
  [`dlls/gdi32/gdi32.spec`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/gdi32/gdi32.spec)
  declares the three GDI aliases (lines 207, 286, 321). Its
  [`dlls/gdi32/dc.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/gdi32/dc.c)
  lines 1887-1907 and 2016-2052 validates DC/metafile state and calls
  `NtGdiGradientFill`, `NtGdiTransparentBlt` and `NtGdiAlphaBlend`.
  [`dlls/win32u/bitblt.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/win32u/bitblt.c)
  lines 841-1050 handles transparent/alpha DC geometry, overlap and driver
  dispatch; [`dlls/win32u/painting.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/win32u/painting.c)
  lines 894-916 validates gradient mesh indices and delegates to its device
  driver. Wine's [`msimg32.spec`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/msimg32/msimg32.spec)
  forwards the three MSIMG32 names **to** modern GDI32 aliases. Those
  forward declarations are not independent Win98 implementations; copying
  that direction into a reverse GDI32-to-MSIMG32 bridge would form a loop.
- ReactOS `9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8`
  [`gdi32.spec`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/gdi/gdi32/gdi32.spec)
  declares GDI aliases at lines 242/299/327.
  [`objects/painting.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/gdi/gdi32/objects/painting.c)
  lines 833-939 dispatches through metafile handling and `NtGdi*` after
  DC conversion. [`ntgdi/bitblt.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/gdi/ntgdi/bitblt.c)
  and [`eng/alphablend.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/gdi/eng/alphablend.c)
  use NT `PDC`, `SURFACE`, `XLATEOBJ` and `SURFOBJ` objects, locks and
  clipping. They cannot be directly linked into Win98 GDI32. Its
  [`msimg32.spec`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/msimg32/msimg32.spec)
  likewise forwards MSIMG32 names to GDI32.
- Pinned KernelEx `31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`
  `apilibs/kexbases/Gdi32/_gdi32_apilist.c:55` maps `GdiAlphaBlend` to
  `GdiAlphaBlend_stub`, and `_gdi32_stubs.c:25` declares
  `UNIMPL_FUNC(GdiAlphaBlend, 11)`: an entry-name/loader aid, **not drawing**.
  `auxiliary/msimg32/msimg32.def` provides `AlphaBlend_NEW`,
  `GradientFill_NEW`, `TransparentBlt_NEW` in a separate DLL.
  `auxiliary/msimg32/msimg32.c` has software/DIB, pen and mask recipes using
  original GDI calls. It is substantive code, but has unverified pixel and
  edge behavior, including unchecked large width×height arithmetic in its
  alpha path and a stated gradient-alpha limitation. It must not be counted
  as proof of compatibility or copied without a fresh behavioral/license
  review.

Wine code is under its per-file LGPL policy; ReactOS sources have per-file
GPL/LGPL notices; KernelEx files above carry GPL-2.0 notices (the auxiliary
file also credits historical Wine authors). The selected project code is
GPL-2.0-only. Prefer a newly written, narrow adapter around a verified OEM
native routine. If a software backend becomes necessary, review each copied
file and dependency/license chain, preserve attribution, and avoid importing
Wine/ReactOS NT driver object layouts by name. The Microsoft CAB member is
**test evidence**, not distributable project code.

## Shared backend and loader route to implement as one unit

Candidate provider: one versioned KernelEx API library with a `GDI32.DLL`
table 0 and sorted named entries for **all three** missing `Gdi*` functions.
Route the three names in each active CORE profile as one reviewed family;
leave the existing OEM MSIMG32 and GDI32 exports alone. The route must
override KernelEx's old `GdiAlphaBlend_stub` for ordinary static imports as
well as `GetProcAddress`. The provider must not statically import its own
missing GDI32 aliases or call an MSIMG32 DLL that forwards back to them.

The first bounded backend hypothesis is: outside `DllMain`, load the
installed **actual** `MSIMG32.DLL`, verify by `GetModuleFileNameA`/hash and
PE forwarder inspection, resolve `AlphaBlend`, `TransparentBlt` and
`GradientFill` by **name**, and call them with the exact Win32 ABI. Native
`MSIMG32` performs the drawing using original GDI primitives; this avoids
inventing pixels until its behavior is measured. The provider should retain
a safe module reference for any cached function pointers and preserve the
native return/error behavior unless a measured contract repair is required.
If this installed library is absent, forward-only, recursive, or fails pixel
tests, a common software raster/DIB backend for the three names is required;
no success-shaped stub or unconditional `TRUE` is acceptable.

The shared contract surface includes HDC identity and clipping; bitmap
formats and palette/color conversion; source/destination coordinates,
stretch and transform modes; source/destination overlap; source alpha
premultiplication and constant alpha; color-key comparison; gradient mesh
indexing and triangle/rectangle filling; invalid pointers/dimensions;
resource cleanup and last-error behavior; and enhanced-metafile/printer
paths where supported. [Microsoft's BLENDFUNCTION contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-blendfunction)
requires `AC_SRC_OVER`, zero `BlendFlags`, 32-bpp premultiplied source for
`AC_SRC_ALPHA`, and combination with `SourceConstantAlpha`.
[GdiTransparentBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gditransparentblt)
uses a color key rather than alpha compositing, and
[GdiGradientFill](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdigradientfill)
ignores the `TRIVERTEX.Alpha` member. These distinctions must survive any
common backend.

## Test plan before claiming the port

1. **Identity and baseline:** On the directly installed guest before a new
   GDI route, record installed MSIMG32 path/hash, PE export RVAs/forwarders,
   direct `LoadLibraryA`/`GetProcAddress` results for all three names, and
   original GDI32 alias absence or KernelEx stub behavior. Verify the
   installed MSIMG32 import closure. Do not treat the CAB receipt alone as
   runtime evidence.
2. **Pixel oracle:** Use small offscreen memory DCs and known 32-bpp DIB
   pixels. Test opaque/constant/per-pixel alpha including alpha 0, 128 and
   255, premultiplied input, stretch, clipping, invalid/negative dimensions,
   same-surface overlap and error codes. Test transparent color key with
   keyed and non-keyed pixels, stretch and 32-bpp alpha-copy distinction.
   Test horizontal/vertical gradients and triangle interpolation, valid and
   invalid vertex indices/modes. Compare direct guest MSIMG32 and the
   eventual GDI32 routed aliases against an independent modern 32-bit
   oracle, accounting for documented device rounding rather than declaring
   any nonzero pixel output a pass. Restore selected GDI objects and delete
   every DC/bitmap after each case.
3. **Loader/PE:** Build a no-CRT i386 PE 4.10 static-import fixture requiring
   the three `GDI32.Gdi*` names, plus a direct provider-table fixture.
   Before-route loader failure is the negative control. After installation,
   inspect IAT target module and execute the full pixel/error suite through
   static imports and direct calls; confirm no recursion into MSIMG32
   forwarders and no new non-OEM native import. Keep application media out of
   the released test package.
4. **Application checkpoint:** Only after guest direct/static behavior passes,
   cold-boot with the three reviewed CORE routes and repeat the Notepad++
   loader probe. Record the next import or application behavior. Passing this
   particular loader edge is not a Notepad++ editing or 90% API-surface pass.

No guest run, CORE edit, software implementation or app-success claim was
performed for this audit.
