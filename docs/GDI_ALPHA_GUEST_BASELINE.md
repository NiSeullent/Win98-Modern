# Installed Win98 MSIMG32 baseline, 2026-09-24

This is a **direct guest measurement**, before any new GDI32 route. The test VM
was `Win98Modern-Accel-128`, running the installed Windows 98 SE 4.10 guest.
Its current `VBox.log` contains `NEM: Created partition` and
`WHvCapabilityCodeHypervisorPresent is TRUE`; the visible serial agent reported
`os=4.10`. All drawing was in private 32-bpp top-down DIB sections. The test
program flushed GDI before reading pixels, restored selected objects and
deleted its bitmaps/DCs. It did not edit `CORE.INI` or the system DLLs.

## Identity and loader behavior

| File or load request | Measured result |
| --- | --- |
| Installed `C:\WINDOWS\SYSTEM\MSIMG32.DLL` | 53,248 bytes; SHA-256 `6a099207614102bf2e98a0055e6d06e3ad6cae8ed4e3c648f2ca08e996a42442`; byte-identical to the OEM `WIN98_33.CAB` member. |
| Installed `C:\WINDOWS\KERNELEX\MSIMG32.DLL` | 7,680 bytes; SHA-256 `e2a644c38cd814a63239b0376cb9921d824c492b8816781ae7f3e8724efabee5`. |
| `LoadLibraryA("MSIMG32.DLL")` | `GetModuleFileNameA` reported the **KernelEx** path. |
| `LoadLibraryA("C:\\WINDOWS\\SYSTEM\\MSIMG32.DLL")` | Still reported the **KernelEx** path. An absolute system path alone did not separate modules with this basename in the current process. |
| OEM bytes copied in this ignored test VM to `C:\M98LAB\M98OEMI.DLL` and loaded by that name | `GetModuleFileNameA` reported `M98OEMI.DLL`, enabling an isolated OEM behavior comparison. This Microsoft DLL is test-only, ignored and absent from source/release packages. |

`AlphaBlend`, `TransparentBlt` and `GradientFill` resolved by name from both
actual modules. PE inspection found code RVAs rather than forwarder strings:
OEM ordinals 2/5/4, and KernelEx ordinals 2/5/4. The native Win98
`GDI32.DLL` lacked all three `Gdi*` alias names in the OEM export manifest;
the guest's `GetProcAddress` returned null for each. The probe is a PE32 i386
console 4.10 executable and imports only functions in the OEM GDI32 and
KERNEL32 export manifest.

## Observed pixels and error cases

The modern 32-bit Windows `MSIMG32.DLL` run supplies an independent pixel
oracle for these small cases. It is a **comparison**, not proof that every
modern GDI behavior is required by Windows 98. Values below are raw 32-bpp
DIB words (`0xAARRGGBB`). Pixel 0 is the target; the second pixel in a 1x1
blend had a `0x11223344` sentinel. The destination for the 2x1 transparent
case had four blue pixels, so pixels 2 and 3 were outside the requested
rectangle.

| Case | Modern oracle | KernelEx MSIMG32 in guest | Renamed OEM MSIMG32 in guest |
| --- | --- | --- | --- |
| Opaque red over blue, 1x1 | `TRUE`, pixel0 `00ff0000`, pixel1 sentinel | Same | Pixel0 same; **pixel1 `11aabbcc`** |
| Constant alpha 128 red over blue, 1x1 | `TRUE`, pixel0 `0080007f`, pixel1 sentinel | Same pixels | Pixel0 same; **pixel1 `11667788`** |
| Constant alpha 0, 1x1 | `TRUE`, pixel0 `000000ff`, pixel1 sentinel | Same | Same |
| Premultiplied source alpha 128, 1x1 | `TRUE`, pixel0 `8080007f`, pixel1 sentinel | Same pixels | Pixel0 same; **pixel1 `21caec0b`** |
| Transparent color key green, 2x1 | `TRUE`, `[000000ff, 00ff0000, 000000ff, 000000ff]` | Same | First two pixels same; **outside pixels `[81d6ddbc, a0000030]`** |
| Transparent key/stretch, 2x1 to 4x1 | `TRUE`, `[000000ff, 000000ff, 00ff0000, 00ff0000]` | Same | Same |
| Horizontal red-to-blue rectangle gradient, first row | `TRUE`, `[00ff0000, 00bf003f, 007f007f, 003f00bf]` | Same | Same |
| Triangle gradient, first row | `TRUE`, `[00ff0000, 00bf3f00, 007f7f00, 003fbf00]` | Same | Same |
| Invalid gradient mesh index 9 with only 2 vertices | `FALSE` | **`TRUE`** | **`TRUE`** |
| Invalid gradient mode `0x77` | `FALSE`, last error 87 | `FALSE`, last error unchanged | `FALSE`, last error unchanged |
| Null destination for alpha/transparent | `FALSE`, last error 6 | `FALSE`, last error 87 | `FALSE`, last error 6 |

The OEM code's writes beyond the requested rectangle reproduced after
explicit `GdiFlush()` and deterministic sentinel initialization. Its
`bddraw = 0` diagnostic lines also appeared on stdout. The KernelEx helper
preserved those sentinels on the measured cases, but its invalid-index and
error reporting behavior still differs from the oracle. Last error after a
successful call is not a contractual result and is deliberately excluded from
the pixel comparison.

## Reproduction and scope

Build with `tools/build-gdi-alpha-native-probe.ps1`. The tested source is
`tests/gdi_alpha_native_probe.c`, SHA-256
`c63de4a8539fec77dc7677b24bc4d9c3e0d6c5ce46881bca564340c62dd67039`.
The reproducible executable SHA-256 is
`f9004fb399fca3125287441a14bb9e34d906db60defbd5c65ae3f8c461fc7f29`.
The guest transport verified that exact uploaded hash. Private ignored
receipts are `build/gdi-alpha-audit/guest-native-kex-v4.json`,
`guest-native-absolute-oem-v4.json` and `guest-native-oem-v4.json` (the last
receipt used the uniquely renamed OEM file). The modern oracle output is
`build/gdi-alpha-audit/host-modern-v4.txt`.

This baseline does not establish a new `GDI32` alias, static KernelEx import
binding, application startup, printer/metafile behavior, every pixel format,
or full API compatibility. A bridge must identify the **actual loaded**
MSIMG32, avoid recursion and address the measured contract gaps before its
family can be counted as behaviorally compatible.
