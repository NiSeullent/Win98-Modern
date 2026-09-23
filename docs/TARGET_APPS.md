# 100% API compatibility target and required app corpus

The completion target is **100% of the Windows API surface and working execution of all five required applications**. This is an aspiration, not a current result. A count of exported names or successful imports is only a preliminary metric: an API counts as compatible only after its documented behavior is tested in the Windows 98 guest. A stub that always succeeds or always fails does not count unless that result is the correct contract for the guest state.

## Fixed reference surface

Use the Microsoft Windows SDK **10.0.28000.2705** (released August 2026) as the initial frozen reference. Its Win32 and WinRT desktop API declarations form the inventory to classify; this includes interface methods as well as DLL exports. The locally installed 10.0.26100.0 SDK can bootstrap the inventory, but a percentage calculated against that older subset must be labeled with its version and must not be called full 28000 coverage. Driver DDIs, .NET APIs, and the independently shipped Windows App SDK are recorded separately because the original project excludes modern application drivers and those interfaces are not all Windows SDK OS APIs. [Windows SDK overview](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/), [SDK release notes](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/release-notes), [desktop API families](https://learn.microsoft.com/en-us/windows/apps/api-reference/).

The inventory must record stable API identity, architecture, minimum OS, interface/contract version, and feature dependencies. A numerator entry requires a guest test for success, failure, memory ownership, thread behavior, and documented edge cases as appropriate. For a native Windows 98 function, this means a guest test of the original API. For a wrapper, this means the same test through KernelEx. No complete denominator or 100% score has been computed yet.

## User-selected application corpus

These programs are separate integration probes. Their static PE import coverage and their actual startup/function tests are reported separately from the full-SDK percentage. Versions below are pinned as of 2026-09-23 where the publisher offered a current release.

| Program | Pinned probe | Publisher evidence | Windows 98 guest implication |
| --- | --- | --- | --- |
| Chromium 150 | Official 32-bit [snapshot revision 1639845](https://storage.googleapis.com/chromium-browser-snapshots/Win/1639845/chrome-win.zip), `chrome.exe` version 150.0.7871.0; ZIP SHA-256 `9c3601da8237f3cb22022cd222f06b92f06d88d0674c4f242d1590bec89af97a` | [Chromium download instructions](https://www.chromium.org/getting-involved/download-chromium/), [platform policy](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/supported_platforms.md), [Chrome minimum Windows 10 and SSE3](https://support.google.com/chrome/answer/95346?hl=en) | An API import audit can prioritize wrappers; startup would also require loader, CRT, sandbox, graphics, and CPU work. No guest run is claimed. |
| Supermium | [144.0.7559.256 R5, 32-bit portable package](https://github.com/win32ss/supermium/releases/tag/v144-r5); ZIP SHA-256 `17acfcdf89ea651905053b50b0fce5a28db19cb2c69ed5579c7b177806ed6d31` | [Project README](https://github.com/win32ss/supermium) targets XP, 2003, Vista, 7, and 8.x; [Win98 request](https://github.com/win32ss/supermium/issues/1488) was closed as not planned | Its own XP-era wrappers are useful research, but Windows 98 support is not established. No guest run is claimed. |
| VLC | [3.0.24, 32-bit Windows ZIP](https://get.videolan.org/vlc/3.0.24/win32/vlc-3.0.24-win32.zip); ZIP SHA-256 `8511356afd680817f3aea624c63032d0936f3d77b2175fb36c8fc16adf9744e8` | VideoLAN's [Windows requirements](https://www.videolan.org/vlc/download-windows.html) explicitly mention KernelEx for Windows 95/98/Me | Best first real application probe after KernelEx guest installation. Current version still needs a measured import/runtime test. |
| Notepad++ | [8.9.8, 32-bit portable ZIP](https://github.com/notepad-plus-plus/notepad-plus-plus/releases/tag/v8.9.8); ZIP SHA-256 `880c5c5323305154aaed3fa5238d6186cda3bcfa3a82645243c1e7999e35602a` | [Publisher support table](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/SUPPORTED_SYSTEM.md) says v6.0 was the last release running natively on Windows 98 | Current x86 release is an import and runtime extension target; existing v6.0 support is only a baseline. |
| VS Code | [1.138.0, Windows x64 archive](https://update.code.visualstudio.com/1.138.0/win32-x64-archive/stable); downloaded ZIP SHA-256 `c0a9f12a0d8962fa4cda1959eecd5fdd3f82de9715ad4dadd5601856bb60fd21` | [Publisher requirements](https://code.visualstudio.com/Docs/supporting/requirements) require 64-bit Windows; [FAQ](https://code.visualstudio.com/docs/supporting/faq) identifies 1.83 as the last stable 32-bit release | Current x64 PE cannot load in 32-bit Windows 98. Its API inventory can be studied, but API wrappers alone cannot turn it into a guest executable. |

All five archives were downloaded into ignored `benchmarks/media/`; the Chromium snapshot matched its Google Storage MD5 metadata, while Supermium, VLC, and Notepad++ matched hashes published on their release/download pages. The VS Code hash above is a local digest of the official HTTPS download, not a separately published publisher checksum. The four x86 entry points have PE machine `0x014C`; VS Code 1.138.0 has `0x8664` (x64).

For each binary, record direct and delay-load imports, dependent DLLs, loader failure, and a reproducible guest test. Import coverage is a triage metric, not the 100% full-API goal and not evidence that the application runs. Each required app needs a separate guest launch and meaningful functionality result before completion is claimed.

## Notepad++ 8.9.8 guest probe

Latest 2026-09-24 result: the installed Win98 SE guest cold-booted with
`M98WRP19.DLL` (89 KERNEL32 table names), SHA-256
`ee96a7d5dfda768f21eb0098b8dafdd4080a018847329e5e145d8086b7a46cd4`.
Six static-import suites passed: InitOnce (4 APIs), threadpool work (5),
threadpool callback extensions (7), SList (7), NLS (2), and FLS/lifecycle (13).
These are **38 focused API contract subsets**, not 38 fully compatible APIs.
The ten-test lifecycle regression bundle also passed the 89-name table smoke,
FLS concurrency/termination tests, and EXE plus system MSVCRT thread-exit
callbacks. Exact source/provider/test hashes are in
`benchmarks/api-guest-evidence-v1.json` and family documents.

Notepad++ itself still does not start. APP_PROBE returned exit 2 and
`LAUNCH_FAIL win32_error=31`; the next displayed missing name is now
`USER32.RemoveClipboardFormatListener` (`build/guest/npp-after-fls19.png`).
The previous FlsAlloc import no longer blocks loading. The clipboard listener
and message-delivery family belongs in the catalogue dependency plan; this
loader progression is not application functionality. The remaining targets were checked as below; no required app has a new
functionality pass.

Earlier provider16 stopped at `InitializeSListHead`. A preceding warm restart
of a combined change produced one VxD exception preserved in a separate
snapshot. Later normal shutdown/cold boots passed; repeated warm-restart
reliability remains unverified. During provider17 testing an early host pipe
request before the guest remote agent was ready required transport recovery
and another normal guest shutdown. This is not an API test pass or failure.

The first x86 portable build attempt in the Windows 98 SE + KernelEx guest stopped at a missing `DBGHELP.DLL` loader dialog (`vm/npp-first-run.png`). Its direct `DBGHELP.DLL` import is `ImageNtHeader`. The OEM Windows 98 `IMAGEHLP.DLL` already exports that function (see `benchmarks/win98se-ko-oem-native-exports-v1.json`), so `build/dbghelp.dll` provides the same export and calls the installed native implementation. This bridge passes a PE32 Windows 98 import gate and a 32-bit Windows host smoke test, including a malformed PE image returning `NULL`.

With the bridge beside `notepad++.exe`, the next guest loader dialog reported missing `DWMAPI.DLL` (`vm/npp-shim-result.png`). An app-local DLL exports the two directly imported DWM functions and reports disabled composition. After initial shims the guest showed a generic invalid-format error (`vm/npp-new-alert-upper.png`); changing the executable's PE version fields in an **ignored local test copy** did not help (`vm/npp-pe410-error-revealed.png`). A clean retry with app-local BCRYPT exposed the specific issue: `DWMAPI.DLL` had no base relocation directory, so Win98 could not load it after another shim occupied the preferred base (`vm/npp-after-bcrypt.png`). The DWM build now forces a real base relocation; its PE gate, host smoke, and Win98 guest direct smoke pass (`vm/dwm-reloc-guest-smoke.png`).

The seven direct BCRYPT imports are provided by an app-local SHA-256/MD5/HMAC subset. Its PE gate, host known-answer tests, and Win98 guest direct smoke pass (`vm/bcrypt-guest-smoke.png`). MD5/SHA256 and HMAC pseudo-handle paths also passed a direct guest smoke (`vm/pseudo-guest-smoke.png`). With DBGHELP, relocatable DWMAPI, and BCRYPT present, Notepad++ next stopped at the absent `SHCreateItemFromParsingName` export in native `SHELL32.DLL` (`vm/npp-after-dwm-reloc.png`). A new KernelEx Shell API library now supplies `SHCreateItemFromParsingName`, `SHParseDisplayName`, and the focused file-system case of `SHOpenFolderAndSelectItems`; direct and static-import guest probes passed, and Explorer selected the test file (`vm/select-shell3-guest.png`). The following Notepad++ loader error is `UXTHEME.DLL!DrawThemeTextEx` (`vm/npp-after-uxtheme.png`). A no-theme bridge passed direct guest testing, but KernelEx's KnownDLL redirection bypasses an app-local `UXTHEME.DLL`, so the app has **not** started. None of the publisher binaries or test ISOs is distributed by this project.

## Provider19 five-application checkpoint

Exact executable hashes, PE architectures and receipts are recorded in
`benchmarks/app-guest-checkpoint-provider19.json`. App media remain ignored.

| Selected application | Actual current result | Next shared prerequisite |
| --- | --- | --- |
| Chromium 150 x86 | Loader error 31; CHROME_ELF.DLL requires KERNEL32.AddVectoredExceptionHandler | Exception dispatch and vectored-handler family |
| Supermium 144 R5 x86 | After selecting its version directory as cwd, loader error 31; P_NTD.DLL requires NTDLL.LdrGetProcedureAddress | NT loader/export-resolution backend and bundled wrapper dependency closure |
| VLC 3.0.24 x86 | Process launches but only a failure dialog appears: invalid options or no plugins found. Same result with application directory as cwd. No player UI or playback | Plugin discovery/loading and full dependent API contracts |
| Notepad++ 8.9.8 x86 | Loader error 31 at USER32.RemoveClipboardFormatListener after FLS integration | Clipboard listeners, viewer chain, messages and window lifetime |
| VSCode 1.138.0 x64 | Local PE32+ AMD64 inspection; not launched in x86 Win98 | x64 execution plus user API/loader architecture path |

VLC's APP_PROBE exit 0 means the observation tool completed and closed the
observed process. Its screenshot shows an error dialog, so it is explicitly
not an application success. Supermium's first root-cwd attempt could not find
CHROME_ELF.DLL; using the packaged version directory resolved that layout issue
and exposed the NT loader prerequisite. This does not establish installation
or application compatibility. No target version was replaced by an older one.
