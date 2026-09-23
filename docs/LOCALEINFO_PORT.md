# GetLocaleInfoEx Windows 98 NLS bridge

## Observed blocker and implemented range

After the `FindFirstStreamW` import was supplied, the directly installed
Windows 98 SE guest showed Notepad++ 8.9.8's next missing loader import as
`KERNEL32.DLL!GetLocaleInfoEx` (`vm/npp-after-firststream.png`). This addition
provides the four-argument ABI through the sorted KernelEx KERNEL32 API table.
Resolving a loader symbol does not establish that Notepad++ starts.

The bridge maps `NULL` to the user LCID, `!x-sys-default-locale` to the
system LCID, and validated language or language-region names to installed
Win98 LCIDs with the synchronized `EnumSystemLocalesA` lookup already used by
the date and time ports. It asks original Win98 `GetLocaleInfoA` for legacy
locale fields and decodes text with that locale's installed ANSI code page.
String sizes and return values count UTF-16 code units including NUL, while
temporary ANSI buffers use byte counts. A too-small buffer is left untouched.
The bridge refuses an unavailable code page instead of decoding with the
process default and silently changing the text.

The following non-text forms have separate handling:

- `LOCALE_RETURN_NUMBER` reads the native DWORD, writes two UTF-16 units of
  binary storage, and returns 2, including for a size query. It relies on
  Win98 `GetLocaleInfoA` to validate that the requested field supports a
  numeric return.
- `LOCALE_FONTSIGNATURE` copies the native 32-byte `LOCALESIGNATURE` as
  binary, uses a 16-WCHAR capacity, and returns 16. It is never sent through
  an ANSI-to-Unicode converter.
- Win98 has no locale-name LCType. For validated installed ISO locales,
  `LOCALE_SNAME` and `LOCALE_SPARENT` are formed from the original database's
  ISO language and country codes. A neutral `en` reports name `en` and an
  empty parent; `en-US` reports name `en-US` and parent `en`. These results
  do not cover modern script, sort, custom, or extension tags.

For ordinary fields on a neutral name such as `en`, Win98 supplies the
selected installed regional LCID. That field may differ from an NT system's
distinct neutral-locale data; only the two name fields above preserve their
neutral identity.

`L""` denotes the invariant locale in the Ex contract. Win98 has no
invariant NLS record; this port returns `ERROR_NOT_SUPPORTED` instead of
substituting `en-US`. Modern or unavailable names fail explicitly. A
`LOCALE_RETURN_GENITIVE_NAMES` request fails with `ERROR_INVALID_FLAGS`
because Win98 ANSI locale data cannot supply its Windows 7-era genitive month
forms. Unknown high flags are rejected. A native Win98 locale field that is
not available remains a failure; the bridge does not invent its value.
`LOCALE_USE_CP_ACP` is ignored for the Unicode Ex result while the ANSI
intermediate remains decoded with the locale's code page.

## Source and license review

- [Microsoft `GetLocaleInfoEx`](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getlocaleinfoex) defines the name constants, size query, UTF-16 unit counts, DWORD and font-signature binary examples, and buffer errors. [Microsoft's return constants](https://learn.microsoft.com/en-us/windows/win32/intl/locale-return-constants) restrict `LOCALE_RETURN_NUMBER` to numeric fields and date genitive names to Windows 7 or later.
- Pinned [Wine `dlls/kernelbase/locale.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/locale.c) resolves a locale name into its Unicode NLS database before looking up a field. Its [locale tests](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernel32/tests/locale.c) distinguish neutral `LOCALE_SNAME` and `LOCALE_SPARENT` results.
- Pinned [ReactOS `GetLocaleInfoEx.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32_vista/GetLocaleInfoEx.c) maps names to LCIDs, calls `GetLocaleInfoW`, and specially handles those two neutral-name fields. Its NT Unicode locale data and Vista name resolver are not available on Win98.

The implementation is new GPL-2.0-only project code; no Wine or ReactOS code
was copied. Its native `GetLocaleInfoA`, LCID, code-page, and conversion
imports are present in the pinned Korean Windows 98 SE OEM KERNEL32 export
manifest. The PE gate checks the actual linked DLL against that manifest.

## Verification boundary

Run `./tools/build-localeinfo.ps1` at the repository root. It produces
`build/localeinfo/m98wrap.dll`, a direct KernelEx table test, and a static
`KERNEL32.GetLocaleInfoEx` import probe. The host tests pass for English
decimal and numeric fields, Korean DBCS text and WCHAR size, binary font
signature, neutral and regional names, the two default-locale selectors,
short and null buffers, unknown or script-qualified names, invariant name,
genitive flag, and unknown flags. The PE gate checks PE32/i386 Windows 98
subsystem 4.10, actual native DLL imports, and probe import shapes. The
existing sampled table smoke passes with 43 entries.

Two isolated builds on 2026-09-24 produced the same DLL SHA-256:
`337f4e2e761bcb8e02046a5e9b3a3a44778f67f06e5a04b524e8b2fcc16ec550`.

The static import probe on the modern host resolves the host's native
KERNEL32 implementation. It proves the intended PE import and behavior on
that host, while the direct test invokes this project's DLL.

On 2026-09-24, the directly installed Windows 98 SE guest called the exact
DLL above from `C:\M98LAB\M98WRAP.DLL`: `LOCISM.EXE` returned `PASS:
GetLocaleInfoEx direct Win98 NLS subset` with exit code 0. The same DLL was
installed as versioned `C:\WINDOWS\KernelEx\M98WRP8.DLL`; after normal
shutdown and cold boot on the current WHPX/NEM hardware backend,
`LOCIIMP.EXE` returned `PASS: KERNEL32 static GetLocaleInfoEx behavior` and
`SMOKE43.EXE` returned `PASS: 43-entry KernelEx table and sampled API
behavior`, both with exit code 0. The versioned DLL transfer was verified
against SHA-256 `337f4e2e761bcb8e02046a5e9b3a3a44778f67f06e5a04b524e8b2fcc16ec550`.

The next Notepad++ 8.9.8 guest launch still failed (`win32_error=31`); the
loader dialog now names `KERNEL32.DLL!GetApplicationRestartSettings`
(`vm/npp-after-localeinfo.png`). This establishes that the loader passed the
prior `GetLocaleInfoEx` import, not that this application starts or that all
Win98 locales match modern Windows behavior. Wider locale-data coverage and
repeat warm-restart stability remain separate checks.
