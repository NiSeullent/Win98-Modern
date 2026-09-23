# GDI32 alpha, transparent and gradient drawing bridge

Status: three new `GDI32.DLL` names are implemented as one KernelEx API
library (`M98GDI.DLL`, with the revised candidate installed under the
versioned name `M98GDI2.DLL`). The first guest checkpoint used `M98GDI1.DLL`.
The host build, PE/import gate, provider table and absent-backend behavior
pass. In the installed Windows 98 guest, the first provider build (SHA-256
`4e0fef1de6b9807e7db7056a7937cbafeaf8a20d59cb9dd81e0b0f4a8da5f468`)
passed both the direct provider-table and cold-boot routed static
`GDI32.DLL` 14-case pixel/error contracts with exit 0 and zero internal
failures. The separate three-name static import probe also returned exit 0.
A subsequent host test found that unloading this build leaked one TLS slot;
the revised provider (SHA-256
`c6db6ed0898ff2e0a884771de706d5d854019ba859605dc97ae8d9588237cc9b`)
now frees it and passes 16 repeated load/call/unload cycles on the host.
The revised binary also passed a fresh direct 14-case contract and the
16-cycle DLL lifetime probe in the guest, with exit 0 in
`build/gdi-alpha/guest-v2-direct-contract.json` and
`build/gdi-alpha/guest-v2-lifetime.json`. **Its migrated routed static import
contract still needs a cold-boot retest.** These
14-case results are not complete GDI32, MSIMG32, or application-functionality
coverage. The installed guest's native backend baseline is independently recorded in
`docs/GDI_ALPHA_GUEST_BASELINE.md`.

## Selected API surface and backend

`get_api_table` exposes exactly these sorted `GDI32.DLL` table 0 entries:

| Routed name | Backend in installed KernelEx auxiliary `MSIMG32.DLL` | ABI |
| --- | --- | --- |
| `GdiAlphaBlend` | `AlphaBlend` | `BOOL WINAPI`, 11 x86 words, `BLENDFUNCTION` by value |
| `GdiGradientFill` | `GradientFill` | `BOOL WINAPI`, 6 x86 words |
| `GdiTransparentBlt` | `TransparentBlt` | `BOOL WINAPI`, 11 x86 words |

The catalogue's `gdi-alpha-raster` batch includes both the three new GDI32
aliases and the three existing MSIMG32 backing functions. Other GDI32, MSIMG32,
`Eng*`, Win32U and driver contracts stay in their own queues. Native MSIMG32
presence is not counted as an implemented modern GDI alias.

The adapter loads only `MSIMG32.DLL` beside its own provider DLL using a full
path and checks that the returned module path matches. It requires the
**installed KernelEx auxiliary version** observed in the guest: SHA-256
`e2a644c38cd814a63239b0376cb9921d824c492b8816781ae7f3e8724efabee5`,
PE timestamp `0x4ec18837`, image size `0x5000`, and independent code export
RVAs `0x1020`, `0x1470`, `0x1c10`. Runtime checks verify the PE layout and
that `GetProcAddress` returns each code RVA, outside the export-forwarder
range. The SHA receipt is checked during installation/guest validation; the
runtime PE fields are a version gate, not a cryptographic hash. Each call
holds a `LoadLibraryA` reference until the native function returns; a
thread-local guard rejects reentry. Nothing loads under `DllMain`.

The original Korean Windows 98 SE system `MSIMG32.DLL` was considered, and
loading it under a distinct basename did reach its code. Its 14-case guest
comparison passed only 10 pixel cases. It modified a second destination
pixel outside a 1×1 alpha rectangle and two pixels outside a 2×1 transparent
rectangle. For this measured reason the provider does **not** use that OEM
binary and no Microsoft code is copied or distributed. The installed KernelEx
auxiliary backend matched the modern 32-bit pixel oracle in 14/14 baseline
cases, including untouched pixels just outside each rectangle. These checks
cover the specific pixels and inputs in the baseline, not the whole drawing
contract or every installed KernelEx build.

## Guarded contract subset

The pinned KernelEx auxiliary source uses signed width×height arithmetic
and unchecked intermediate GDI resource creation in some paths. The adapter
rejects zero/negative dimensions, dimensions above 8192, source or destination
areas above 4,194,304 pixels, and coordinates outside ±1,048,576 before
calling it. Those limits prevent integer overflow and very large temporary
surface requests; low-memory allocation failures inside the auxiliary DLL
remain a limitation. Unsupported `BLENDFUNCTION` operation, flags or alpha
format combinations fail with `ERROR_INVALID_PARAMETER`. A null source or
destination HDC returns `FALSE` with `ERROR_INVALID_HANDLE`, matching the
measured modern error rather than the auxiliary DLL's `ERROR_INVALID_PARAMETER`.

For gradients the adapter accepts horizontal/vertical rectangle and triangle
modes, up to 4096 vertices and 1024 meshes. It checks readable arrays, every
mesh index, vertex coordinates within ±32767, spans within 8192, and a
1,048,576-pixel cumulative raster budget before dispatch. The rectangle
budget multiplies its width and height so a long strip cannot authorize an
unbounded perpendicular drawing span. An out-of-range mesh index now
returns `FALSE` and preserves the caller's last error, matching the measured
modern oracle. The unguarded auxiliary DLL had returned `TRUE` after reading
past the supplied vertex array. Unsupported modes return `FALSE` with
`ERROR_INVALID_PARAMETER`. These limits mean many large or unusual valid
Windows drawing requests are **not yet compatible**. Gradient alpha,
printer/metafile behavior, transform/clipping modes, overlapping source and
destination, palette conversion, arbitrary pointer faults and allocation
failure still need dedicated tests or a software backend.

The provider passes the native function's return value and last error through
after cleanup. It does not convert every legacy success to a modern success.
The lazy TLS recursion guard is released at DLL process detach; the
host-side 16-cycle lifetime test failed against the previous build and passes
against the revised candidate.

## Source lineage and build

The independent adapter in `src/m98_gdi_alpha.c` was written after reviewing
the pinned Wine `dlls/gdi32/dc.c`, `dlls/win32u/bitblt.c` and
`dlls/win32u/painting.c`, ReactOS `gdi32/objects/painting.c`,
`ntgdi/bitblt.c` and `eng/alphablend.c`, and KernelEx
`auxiliary/msimg32/msimg32.c` at the revisions named in
`docs/GDI_ALPHA_FAMILY_AUDIT.md`. Wine/ReactOS modern MSIMG32 forward **to**
GDI32 and their NT graphics objects do not implement a Win98 backend. No
upstream body was copied. New source is GPL-2.0-only; the installed KernelEx
auxiliary DLL is an external GPL-2.0 dependency and is not embedded in the
new provider.

Run `./tools/build-gdi-alpha.ps1`. It builds the shipping provider, an
identical direct-table fixture, a host fail-closed test, a static-import
probe, and matching direct/static 14-case pixel contract probes. The PE gate
requires i386 PE32 Windows 98 subsystem 4.10, base relocations in both DLLs,
no TLS/delay/CLR directory, only original-media native imports in the
provider, and three genuine static GDI32 imports in the routed probes. The
host executable checks table names/order/pointers and confirms a missing
sibling backend fails with `ERROR_MOD_NOT_FOUND` (126) on current NT hosts or
`ERROR_DLL_NOT_FOUND` (1157) in the installed Win98 guest. The guest's
`GDIFIX.DLL` diagnostic recorded all three calls returning `FALSE` and 1157
with no backend in its directory; see
`build/gdi-alpha/guest-absent-diagnostic.json`. This fail-closed result is
separate from the successful direct pixel contract through the provider in
the KernelEx directory.

The first installed guest run used a hash-verified `M98GDI1.DLL` and the pinned
KernelEx auxiliary backend. `build/gdi-alpha/guest-direct-contract.json`
records the pre-route direct pass. After adding `gdi-alpha-raster` to all
three active CORE profiles through `remote/patch_core_family.py` with
`--library m98gdi1 --register-provider` and cold-booting, the static contract
passed in `build/gdi-alpha/guest-static-contract.json` and the three-name
static import probe passed in `build/gdi-alpha/guest-import-probe.json`.
The routed contract's stdout SHA-256 is
`b331b321dbb9a72c65b30ddff4427e57d5411ca3cbcb51020b3b4bb543170688`.
The TLS-fixed DLL is installed as `M98GDI2.DLL` beside the retained first
version. Its direct contract probe loaded that exact path and passed. The
guest route was migrated from `m98gdi1` to `m98gdi2` across all active
profiles and cold-booted. `build/gdi-alpha/guest-v2-static-contract.json`
records all 14 static GDI32 contract cases passing with
`internal_failures=0`; `build/gdi-alpha/guest-v2-import-probe.json` records
the separate three-name static import pass. The installed USER32 clipboard
suite also passed 6/6 after this route change in
`build/guest/suite-clipboard-after-gdi2.json`. These receipts certify the
versioned binary's limited tested contracts, not arbitrary raster inputs.

With those first-build routes, Notepad++ 8.9.8 crossed its prior
`GDI32.DLL!GdiAlphaBlend` loader failure: `APP_PROBE` launched the process
and observed one `Configurator` dialog for 30 seconds. A live screenshot
showed `Load langs.xml failed!` in that dialog. The app was running from
the read-only `D:\NPP` ISO. The editor window did not appear in this run,
so this is a loader/startup checkpoint rather than Notepad++ functionality.
The read-only package contained `langs.model.xml` but raised
`Load langs.xml failed!`; copying its 25.9 MB folder into writable
`C:\M98LAB\NPP` changed the observed modal to `Exception On WM_CREATE` /
`Access violation`. That is a new application failure requiring diagnosis;
no cause is assigned to the GDI bridge from this dialog alone. The captured
records are
`build/gdi-alpha/guest-npp-after-gdi.json` and
`build/gdi-alpha/guest-npp-after-gdi-visual.json`, plus the writable-copy
probe `build/gdi-alpha/guest-npp-writable.json` and
`build/gdi-alpha/npp-writable-visual.png`. No whole-family or
whole-API compatibility percentage follows from these three ports.
The same writable app folder under the TLS-fixed `m98gdi2` route again
showed the `Exception On WM_CREATE` access-violation dialog for the full
30-second observer interval; see `build/gdi-alpha/guest-npp-v2-writable.json`
and `build/gdi-alpha/npp-v2-writable-visual.png`. The exception tracer's
separate record and module analysis are in
`docs/NPP_WM_CREATE_CRASH_TRIAGE.md`.

An independent review found that the rectangle gradient budget previously
counted only one axis although the backend draws the full perpendicular span
for each step. The shipping correction rejects a 1024×1025 rectangle while
allowing 1024×1024 at the one-million-pixel boundary in a host-side argument
test. The corrected DLL SHA-256 is
`d61826f03a21e04ba5a983ee99db8a9cfff3ce33aee7832e10086f7581bc5ac2`.
Installed as `M98GDI3.DLL`, it passed the direct 14-case contract in
`build/gdi-alpha/guest-v3-direct-contract.json`, then the static GDI32
14-case contract and separate three-name import probe after migration of nine
CORE routes and another hardware-accelerated cold boot. Those receipts are
`build/gdi-alpha/guest-v3-static-contract.json` and
`build/gdi-alpha/guest-v3-import-probe.json`. The compact public record
`benchmarks/gdi-alpha-v3-evidence.json` pins the corrected provider, source,
test binary, CORE hash and static receipt.
The USER32 clipboard regression suite passed 6/6 after this route change in
`build/guest/suite-clipboard-after-gdi3.json`. A 15-second writable-folder
Notepad++ regression again showed only `Exception On WM_CREATE` / `Access
violation`, as recorded in `build/gdi-alpha/guest-npp-v3-writable.json` and
`build/gdi-alpha/npp-v3-writable-visual.png`.
