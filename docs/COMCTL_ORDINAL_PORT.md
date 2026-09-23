# COMCTL32 ordinal 345/381 bridge candidate

Status on 2026-09-24: the first provider's **direct table test** and
**cold-boot static COMCTL32 ordinal import probe** passed in the installed
Windows 98 SE guest. The second PNG provider passed those gates and advanced
Notepad++ directly into its editor without the previous `501` icon warning.
Opening, editing, and saving an existing text file were verified by reading
changed bytes back from the guest with both PNG provider revisions. Both a
probe `WM_CLOSE` and a user
`Alt+F4` still produced an invalid-page error. A third provider with
display-depth icon selection also passed direct guest calls, cold-boot ordinal
routing, and a bounded editor startup probe; the shutdown fault persisted.
None of these results establishes complete Notepad++ functionality or full
Common Controls v6 semantics.

## Why this family

The pinned Notepad++ 8.9.8 x86 executable (SHA-256
`960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78`)
imports `COMCTL32.DLL` by ordinals 381 and 345. The Korean Win98 SE OEM
COMCTL32 export manifest lacks both. Two inspected x86 Common Controls 6.0
files map 381 to `LoadIconWithScaleDown` and 345 to `TaskDialogIndirect`; the
local Windows 11 v6.0.26100.9278 file has SHA-256
`abaf51d752f7442c8734810981904419fa2219b23f7f197e8fa279106bad890d`
and confirms both entries. Its sibling v5.82.26100.8521 file lacks both.
This is evidence for the selected application's ABI, not an assertion that
these private ordinal assignments are stable across every COMCTL32 build.

The earlier WM_CREATE trace found an indirect call through the application's
381 IAT cell into a noncode address in the guest. The new routed static probe
and changed Notepad++ modal support that ordinal routing was a necessary
startup repair. They do not prove that every icon form works.

## Source review and design

- The pinned [Wine COMCTL32 spec](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/comctl32/comctl32.spec)
  has neither 345 nor 381. Pinned [ReactOS COMCTL32 spec](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/comctl32/comctl32.spec)
  also omits them, although ReactOS has a full
  [TaskDialogIndirect body](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/comctl32/taskdialog.c#L1252-L1277)
  that depends on its Windows NT common-controls runtime. None of these
  implementation bodies was copied.
- Microsoft's [LoadIconWithScaleDown contract](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-loadiconwithscaledown)
  defines exact-size preference, then a larger source image to scale down
  for nonstandard sizes. Microsoft's
  [TaskDialogIndirect](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-taskdialogindirect)
  and [TASKDIALOGCONFIG](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/ns-commctrl-taskdialogconfig)
  define the four-word x86 call and the 96-byte configuration structure.
- Pinned [KernelEx API table ABI](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/common/kexcoresdk.h)
  supports sorted unnamed entries. Its
  [ordinal configuration parser](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/core/apiconfmgr.cpp)
  recognizes `COMCTL32.381=provider.0`, and its
  [resolver](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/core/resolver.cpp)
  routes static ordinal imports through that table. An app-local replacement
  `COMCTL32.DLL` would have to preserve hundreds of native controls exports
  and risk other components; the new provider leaves the OEM DLL installed.
- VxKex was reviewed as an external Windows 7 loader reference in
  `docs/VXKEX_SOURCE_AUDIT.md` in the full source checkout. Its NT verifier loader is
  not used and its unlicensed code is not copied.

The original GPL-2.0-only source is
[`src/m98_comctl_ordinals.c`](../src/m98_comctl_ordinals.c),
[`src/m98_comctl_ordinals.h`](../src/m98_comctl_ordinals.h), and
[`src/m98ctl.c`](../src/m98ctl.c). `get_api_table` exposes two sorted named
aliases and two sorted ordinal entries, with the aliases sharing the same
function addresses. It imports only original Win98 `KERNEL32.DLL` and
`USER32.DLL` names and never imports COMCTL32 itself.

## Implemented behavior and limits

`LoadIconWithScaleDown` checks its output, name, and positive dimensions
bounded at 1024. For an icon group inside a module, it selects an exact image
first, then the smallest image at least as large in both dimensions, then the
largest remaining image. It creates an owned icon at the requested size using
the original Win98 `CreateIconFromResourceEx`. For `hinst == NULL`, it uses
`LoadImageA`: an icon path uses `LR_LOADFROMFILE`; a stock icon identifier
uses `LR_SHARED` followed by `CopyImage` so the returned handle is owned by
the caller. It rejects lossy Unicode-to-ACP resource/path conversion with
`E_NOTIMPL`. This does not yet reproduce every irregular icon-group tie,
standalone ICO selection, high-DPI, malformed-resource, or 256px PNG case.

`TaskDialogIndirect` validates the 96-byte x86 ABI and implements a **small
functional subset** with original Win98 `MessageBoxA`: plain Unicode title,
instruction, and content convertible without ACP loss, and common button
sets OK, OK/Cancel, Yes/No, Yes/No/Cancel, Retry/Cancel. It maps selected
button IDs, including supported default buttons, and writes zero radio and
unchecked verification outputs after success. Resource strings may be loaded
from `hInstance`. Custom buttons, radio/verification controls, icons,
callbacks, hyperlinks, progress, expando, footer, and all nonzero task-dialog
flags fail with `E_NOTIMPL` **before showing a dialog**. This function must
not be counted as full TaskDialog API compatibility. Its plain-UI branch
still needs an interactive guest test.

## First provider: reproducible gates and guest evidence

Run `./tools/build-comctl-ordinals.ps1`. The first frozen provider
`build/comctl-ord/M98CTL.DLL` has SHA-256
`c86fe113f21b37e834e87996028f3a07788ef2f45af37390b0a914901581c014`;
the direct host fixture `build/comctl-ord/comctl_ordinal_host.exe` has
SHA-256
`87b75a841cef55ac65c1db7a130e5f9d9987986fd91d92ad8d5ff7e464179d4c`.
The host test loads the actual provider table, checks ABI/order/name identity,
null/invalid cases, 16px and 32px system icons, and an embedded two-image
16px-red/48px-blue ICO group. Requests for 16px and 40px confirm the
exact/next-larger image choice and 40px result. The PE gate verifies i386
PE32, Win98 4.10 subsystem, DLL relocations, no TLS/delay/CLR or ASLR/NX,
and all provider imports against the OEM export manifest. The same frozen
provider and host EXE were uploaded to the installed Win98 SE guest and
`C:\M98LAB\CTLHOST.EXE` returned exit 0 with the expected one-line PASS in
`build/comctl-ord/guest-direct-host.json`. This is direct table-call guest
evidence; it does not exercise COMCTL32 static import redirection. The separate
CORE patcher has three host tests covering six ordinal routes, registration,
duplicate rejection, and byte preservation.

`tests/comctl_ordinal_import_probe.c` and its ordinal-only import library
recipe were added after the above frozen provider checkpoint. The resulting
`build/comctl-ord/comctl_ordinal_import_probe.exe` has SHA-256
`676f3458563879dca0e7b1ac97a6d312aff982d7470a393125a8a7bf1e34df98`.
Its PE gate passed and its actual import table contains only COMCTL32
ordinals 381 and 345, plus original Win98 KERNEL32/USER32 names. After the
versioned provider `M98CTL1.DLL` was routed through a cold boot,
`C:\M98LAB\CTLIMP.EXE` returned exit 0 in
`build/comctl-ord/guest-static-probe.json`. It tested both ordinal IAT slots,
the `#345` invalid-argument result, and a `#381` 16px stock icon. That
historical direct probe used an identical copy named `M98CTL.DLL` beside the
host fixture in `C:\M98LAB`; KernelEx used the versioned
`C:\WINDOWS\KernelEx\M98CTL1.DLL`. The initial six routes were generated from
a current guest CORE using `tools/patch_core_ordinals.py`, backed up, and
tested after a cold boot. The earlier GDI2-derived
candidate `build/comctl-ord/core-comctl1-candidate.ini` (SHA-256
`73239b71949ff8c85f2be97bc09cb549007133d90be5f5d549c2abe28a230cb1`)
is a **host patcher fixture only**; it must not replace a newer GDI3 guest
CORE configuration.

This static probe establishes only the tested ordinal calls. The supported
TaskDialog subset, arbitrary icon groups, and application UI behavior need
their own guest contracts.

## Notepad++ icon warning after ordinal routing

`build/comctl-ord/guest-npp-after-ordinals.json` records a modal window titled
`501` after 2.694 seconds and still present after 30 seconds; the screenshot
is `build/comctl-ord/npp-after-ordinals.png`. The application remained alive
until the bounded probe ended it. In a later direct GUI run, selecting **Yes**
on the warning opened a Notepad++ editor
(`build/comctl-ord/npp-direct-after-yes1.png`). Two ASCII lines were typed
successfully (`build/comctl-ord/npp-save-after-type.png`). Ctrl+S and
File → Save / Save As did not produce a visible save dialog, and the title
remained `*new 1`. This is evidence of an interactive editor after ignoring
the icon warning; it is not evidence of file-save success.

The exact text comes from Notepad++ v8.9.8
[`ImageListSet.cpp` `IconList::addIcon`](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/WinControls/ImageListSet/ImageListSet.cpp#L57-L99),
which calls
[`DPIManagerV2::loadIcon`](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/dpiManagerV2.cpp#L226-L234).
That routine first calls `LoadIconWithScaleDown`; on failure it tries native
`LoadImage`. The warning title is the decimal icon resource ID converted to a
string, and [Notepad++ `resource.h`](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/resource.h#L260)
defines `IDI_SAVED_ICON = 501`.

Static PE resource inspection of the pinned `notepad++.exe` found that group
icon 501 **is present**: one 16×16 32bpp entry points to RT_ICON 1705, 805
bytes, beginning `89 50 4E 47 0D 0A 1A 0A` (PNG). Of 2,067 RT_GROUP_ICON
image entries, 2,029 point to PNG payloads and 38 to DIB payloads. Thus the
warning is consistent with an image decoder gap, not with an absent resource.
The first bridge passed the PNG bytes to Win98 `CreateIconFromResourceEx`;
Win98's icon creation path did not return a handle in this app run. The
application's `LoadImage` fallback also returned null. The precise error
value was not captured, so this is a strong source/resource-backed diagnosis,
not a completed direct failure-code probe for RT_ICON 1705.

Pinned [Wine `user32/cursoricon.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/user32/cursoricon.c#L221-L401)
recognizes PNG payloads, decodes them with libpng, and converts them to
bitmap icon data. Pinned [ReactOS `user32/windows/cursoricon.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/user/user32/windows/cursoricon.c#L42-L84)
uses the same PNG-to-bitmap approach with a delay-loaded libpng dependency.
Those NT/Unix dependencies cannot be copied into this Win98 provider. The
next implementation used an independently integrated, permissively licensed
PNG decoder with bounded image dimensions and memory, then fed BGRA pixels
and a binary transparency mask into the Win98 icon creation path.
Do not replace Notepad++ resources or silence its warning, because either
would hide the missing general icon behavior.

## Experimental PNG provider and direct guest gates

`src/m98_icon_png.c` adds a PNG-to-DIB adapter for ordinal 381. It validates
PNG signature and IHDR dimensions, caps each allocation at 8 MiB and width
and height at 1024, decodes into RGBA, writes bottom-up BGRA pixels plus a
1-bit transparency mask, and asks Win98 `CreateIconFromResourceEx` for an
owned icon at the requested size. PNG support is compiled only when
`M98_WITH_PNG` is set. A separate build, `./tools/build-comctl-png.ps1`,
produces `build/comctl-png/M98CTLP.DLL`; it does not overwrite the guest-passed
`build/comctl-ord/M98CTL.DLL`. Rebuilding the old provider without the PNG
flag in a separate directory produced a byte-identical SHA-256
`c86fe113f21b37e834e87996028f3a07788ef2f45af37390b0a914901581c014`.

The decoder is unmodified LodePNG C version 20260119 at pinned repository
commit `ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a`. Its independent
zlib-style license is retained in `src/vendor/lodepng/LICENSE` and the C/H
copyright headers; all three belong in the source package. The Win98
adapter is original GPL-2.0-only source. No Wine or ReactOS implementation
body was copied. Encoder, disk I/O, ancillary-chunk handling, error strings,
and C runtime allocators are disabled at compile time. The adapter supplies
Win98 process-heap allocation. The portable decoder's error 83 is its
documented allocation-failure code.

The first PNG build (`2a130f8d71e533402387f873037d3c1216a77926acb7f54eacd81829b9555119`)
passed two **direct** installed-Win98 tests: generated PNG RT_ICON exact16
and scaled40 in `build/comctl-png/guest-png-direct.json`, and the pinned
Notepad++ PNG icon 501 at 16px in
`build/comctl-png/guest-npp501-direct.json`. Both returned exit 0 through the
COM1 remote tester. `tools/test-comctl-npp501.ps1` extracts group 501 from a
caller-supplied, SHA-verified Notepad++ 8.9.8 executable into an ignored
local ICO fixture, then links a host probe. No application resource is added
to the source package. Its probe EXE SHA-256 is
`0a5a452f05e2e50b5089a9283ec2318a2c50c6dcd93e904a5fd7287dc809fd6e`.

Source review found one selection-rule gap after those direct runs. The
[LoadIconWithScaleDown contract](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-loadiconwithscaledown)
prefers a larger source for a **nonstandard** request, while standard
16/32/48/256 requests use normal closest-source behavior. The documented
[normal icon selection](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-lookupiconidfromdirectoryex)
prefers the closest size that does not exceed the request. A guarded local
selector now applies that standard-size rule before PNG/DIB creation. It
avoids passing an untrusted resource buffer to the native selector, whose
documentation warns that malformed data may fault. The new host test covers
standard16→16px red, standard32→16px red, nonstandard40→48px blue, and
exact48→48px blue.

The second PNG build `M98CTL2.DLL` had SHA-256
`43b612dca21284b9e5d29f769ddddb850e207e74c5665f5948d738b91f4e0a96`.
Its generated-resource host probe has SHA-256
`b767337dd18ecabadc080828cf10722d7e081864eb311101a3931058e803d425`.
The PE/OEM-import gate and both local resource host probes passed. This exact
DLL passed **direct provider-table tests in installed Win98**:
`build/comctl-png/guest-v2-direct.json` returned exit 0 for standard 16/32,
nonstandard 40, and exact 48;
`build/comctl-png/guest-v2-npp501-direct.json` returned exit 0 for the pinned
Notepad++ icon 501. A fresh guest CORE readback had SHA-256
`6ac882aedf6e21d7aad6cbc62805aeda299cdaee65ee1b27af750f9aa53912d1`.
The backed-up M98CTL2 candidate changed seven references and had SHA-256
`1fa8497f6cecba21ad7080ed3b326f2804662024d1e2851ae09988e531d9f693`.
Provider/CORE uploads and readbacks were verified. After a cold boot,
`build/comctl-png/guest-v2-static.json` returned exit 0 for the two static
COMCTL32 ordinal IAT slots and a 16px stock icon through KernelEx.

The second build reached a Notepad++ editor without the prior `501` warning.
`build/comctl-png/guest-npp-after-png.json` saw a `Notepad++` class window
titled `new 1 - Notepad++` at 4.096 seconds and still alive at 15.004 seconds
as `*new 1 - Notepad++`;
`build/comctl-png/guest-npp-during-probe.png` shows the editor. After the
probe posted `WM_CLOSE`, an invalid-page error in `NOTEPAD++.EXE` appeared
(`build/comctl-png/guest-npp-crash-details.png`); the probe ultimately
force-terminated it. This is startup evidence, not clean shutdown.

A separate direct GUI test opened an existing guest text file, entered a new
line, and used Ctrl+S. Reading `C:\M98LAB\NPPTEST.TXT` back through the guest
remote tool returned 43 changed bytes, SHA-256
`14b87c85a9ba983c5eeca1a7871f955473ce0a292456a55c3d3a3c19aa01655f`
(`build/comctl-png/guest-npp-existing-read.json`). The original 21-byte file
had SHA-256
`540871bfa56b09493216a19fca26f5dddb8ee06fd6a6bde0352e7682824f8138`.
That verifies **existing-file open, edit, and save**, while the earlier new
document Save As dialog did not appear. A user `Alt+F4` on the saved document
also reproduced an invalid-page error
(`build/comctl-png/guest-npp-existing-close.png`). New-file save and clean
exit remain unresolved.

## Third PNG candidate: display-depth selection

The second build chose highest declared bpp among equal-size icon images.
Microsoft's
[normal icon selection contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-lookupiconidfromdirectoryex)
instead prefers the display's color depth, then the greatest depth that does
not exceed it, or the lowest depth above it. `src/m98_icon_choice.c` now
implements this as a bounded pure selector. The provider reads
`BITSPIXEL × PLANES` from the current screen DC through OEM USER32/GDI32;
it uses 32bpp if that query fails. `tests/comctl_icon_choice_contract.c`
injects 4, 8, 16, 24, and 32bpp display depths and tests exact, below, above,
standard-size, and malformed-resource cases. The current host machine's
32bpp condition also passed a true duplicate-size PNG resource probe.
`windres` normalizes ICO directory bpp when compiling `ICON`, so this fixture
embeds raw, bounded RT_GROUP_ICON/RT_ICON resources with distinct declared
16/32bpp metadata and red/blue payloads. The guest probe prints its actual
`DISPLAY_BPP` and verifies the selected color without changing screen mode.

The **current** `build/comctl-png/M98CTLP.DLL` has SHA-256
`aafd97b71ff63c9f9cedffa4705713243d5708f22ff6ffaf04c732261ca0e3f8`.
The direct display-depth probe
`build/comctl-png/comctl_png_depth_host.exe` has SHA-256
`07fe5673aa3b36bec8e28b2513af012b337d0aaf9c3441f7103d74458448237e`.
The PE/OEM-import gate, generated-resource icon host probe, actual NPP 501
host probe, display-depth host probe, and injected contract probe all pass
with this exact DLL. The **same hash** passed installed-Win98 direct calls:
`build/comctl-png/guest-v3-direct.json` for standard/nonstandard image sizes,
`build/comctl-png/guest-v3-npp501-direct.json` for the actual Notepad++ icon,
and `build/comctl-png/guest-v3-depth-direct.json` for duplicate-size color
depth selection. The guest reported `DISPLAY_BPP=4` and chose the 16bpp red
image, the lowest candidate above its display depth. Physical guest screen
modes of 16/32bpp have not been tested; those branches have injected host
contract evidence only.

A fresh M98CTL2 guest CORE readback had SHA-256
`1fa8497f6cecba21ad7080ed3b326f2804662024d1e2851ae09988e531d9f693`.
The backed-up M98CTL3 candidate and guest readback both had SHA-256
`eeaa555431c3d31d997afa0d0dee1abcd667c4afcbce2dcabcf56df3317cabea`.
After cold boot, `build/comctl-png/guest-v3-static.json` returned exit 0 for
COMCTL32 static ordinals 345 and 381 and a stock icon. The bounded
`build/comctl-png/guest-v3-npp-app.json` probe saw a `Notepad++` window at
3.974 seconds and still alive at 12.004 seconds, with the saved
`C:\M98LAB\NPPTEST.TXT` session restored and no `501` modal observed. The
probe's `WM_CLOSE` again led to the invalid-page error and forced termination
(`build/comctl-png/guest-v3-npp-during.png`). This confirms startup and
session restore for this hash, not clean shutdown.

The same M98CTL3 installation then opened an existing 13-byte
`C:\M98LAB\NPPV3.TXT`, inserted `Saved under M98CTL3`, and saved it with
Ctrl+S. The guest remote readback reported 34 bytes and SHA-256
`a1853e54f7e131411c6f0787e80d8f7157f3e60ce85438ddfc427c5b82bf1c2c`
(`build/comctl-png/guest-v3-save-read.json`), matching the changed text on
disk. This verifies **existing-file open, edit, and save on the current
provider hash**. The screenshots are `build/comctl-png/guest-v3-save-open.png`
and `build/comctl-png/guest-v3-save-after.png`. New-file Save As remains
unverified for this hash, and clean shutdown still fails as described above.
The 256px icon path, alpha rendering, malformed PNG handling, and icon
lifetime remain unverified.

## Guarded CORE.INI upgrade to M98CTL3

`tools/upgrade_core_comctl_png.py` requires a fresh copy of the current guest
`CORE.INI`, its expected SHA-256, the exact current DLL hash above, and an
explicit `--from-provider m98ctl1` or `m98ctl2` with
`--to-provider m98ctl3`. It rejects changed or duplicate/missing routes,
unrelated uses of the old provider, an already registered `m98ctl3`, hash
mismatches, and existing output paths. It creates a separate byte-identical
backup and a candidate in which exactly the DCFG1 registration and six
ordinal routes change. It re-reads all files and reports their hashes. It
never edits the source or installs guest files. For a CORE with no prior
COMCTL provider, use `tools/patch_core_ordinals.py --provider m98ctl3` to
create the initial six routes.

Run from the repository root after saving and hashing a **fresh guest
readback**, replacing `SHA256_OF_FRESH_GUEST_COPY` with its lowercase digest:

```powershell
python tools/upgrade_core_comctl_png.py build/guest/core-current-readback.ini build/comctl-png/core-m98ctl3-candidate.ini --backup build/comctl-png/core-before-m98ctl3.bak --provider-dll build/comctl-png/M98CTLP.DLL --expect-source-sha256 SHA256_OF_FRESH_GUEST_COPY --from-provider m98ctl2 --to-provider m98ctl3
```

The three upgrade tests pass for both predecessor versions. The guest
integrator used the M98CTL2→M98CTL3 path and verified the fresh source,
backup, candidate, guest readback, cold boot, static ordinal probe, bounded
application startup, and existing-file save described above.
