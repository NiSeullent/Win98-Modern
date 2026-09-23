# GetDateFormatEx Windows 98 port

## Scope

The installed Windows 98 SE guest advanced past the `GetTimeFormatEx` and
`GetProductInfo` loader imports, then Notepad++ 8.9.8 reported the next missing
static import: `KERNEL32.DLL!GetDateFormatEx`. This change adds that seven
argument ABI to the sorted KernelEx KERNEL32 API table. The loader observation
is not an application startup result.

The new provider resolves an installed locale name to a Win98 LCID with the
same synchronized `EnumSystemLocalesA` lookup as `GetTimeFormatEx`. It checks
the date with `SystemTimeToFileTime` and obtains the correct day of week with
`FileTimeToSystemTime`, ignoring time fields as the contract requires. It calls
the original Win98 `GetDateFormatA` and converts its result from the selected
locale's ANSI code page to UTF-16. ANSI allocations use byte counts; the
return value and buffer capacity use UTF-16 code units including NUL. A custom
Unicode picture must round trip through the code page, or the call fails with
`ERROR_NO_UNICODE_TRANSLATION` rather than silently replacing text. A short
output buffer is left untouched.

The invariant locale uses fixed short, long, and year/month pictures. For
`DATE_YEARMONTH` on other locales, the wrapper asks the installed NLS database
for `LOCALE_SYEARMONTH` and then formats with that picture. If Win98 lacks that
locale value, it fails explicitly with `ERROR_INVALID_FLAGS`. The newer
`DATE_MONTHDAY`, layout, and directional-mark options are also rejected rather
than reported as successful without their semantics. `lpCalendar` is reserved
and must be `NULL`.

## Source review and licensing

- [Microsoft `GetDateFormatEx`](https://learn.microsoft.com/en-us/windows/win32/api/datetimeapi/nf-datetimeapi-getdateformatex) specifies the seven arguments, reserved calendar parameter, style flags, date validation, and UTF-16 length query. [Microsoft `GetDateFormatA`](https://learn.microsoft.com/en-us/windows/win32/api/datetimeapi/nf-datetimeapi-getdateformata) specifies the LCID/ANSI interface used by the bridge.
- Pinned Wine [`dlls/kernelbase/locale.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/locale.c) uses its NT locale data and Unicode formatter. Its `GetDateFormatEx` checks negative/null output capacity and the reserved calendar pointer before formatting. Its date formatter selects locale pictures and validates dates.
- Pinned ReactOS [`dll/win32/kernel32/winnls/string/lcformat.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/winnls/string/lcformat.c) resolves locale names to LCIDs and calls its Unicode NLS formatter. That formatter rejects incompatible styles and recomputes the day of week. Its NT NLS tables and era engine are not available on Windows 98.

No Wine or ReactOS code was copied for this addition. The bridge is original
GPL-2.0-only project code. All three newly imported native functions,
`GetDateFormatA`, `SystemTimeToFileTime`, and `FileTimeToSystemTime`, appear in
the pinned Korean Windows 98 SE OEM `KERNEL32.DLL` export manifest. The PE
gate checks the actual linked DLL against that manifest.

## Verification

Run `./tools/build-dateformat.ps1` from the repository root. It creates
`build/dateformat/m98wrap.dll`, a direct API-table smoke executable, and a
static `KERNEL32.GetDateFormatEx` import probe. On the host, the PE32/i386
Windows 98 loader gate, direct behavior tests, and static import probe pass.
Direct tests cover fixed and default dates, incorrect day of week, ignored
time members, Korean DBCS pictures, UTF-16 sizing, invariant names, invalid
dates, style conflicts, reserved calendar, and buffer errors. The static
probe runs against the host's native KERNEL32, so it proves import shape and
host behavior only. The existing 40-entry API-table smoke also passes with
the new DLL on the host.

On 2026-09-24 the exact DLL SHA-256
`2ac2e49d725a4977901e7ebf363a0ee038c20de445b8d416d268dcb1534f7156`
was transferred to the directly installed Windows 98 SE guest as versioned
`M98WRP4.DLL` and as the app-directory direct-test DLL. The direct
date-format smoke and 40-entry sampled API-table smoke both passed. A
byte-preserving `CORE.INI` patch changed only the KERNEL32 API library token;
after a normal shutdown and cold boot to the GUI, the static
`KERNEL32.GetDateFormatEx` import probe passed with exit 0. Notepad++ then
reported the next loader blocker, `KERNEL32.GetFinalPathNameByHandleW`
(`vm/npp-after-date.png`). This is **not** app startup. Locale data and
alternate-calendar behavior beyond the tested cases remain to be measured.
The same WRP4/UXTNEW2/M98ADV configuration then completed one normal GUI
Restart and returned to the desktop; the COM1 guest agent answered a ping.
Earlier combined changes had caused a VxD exception on a different restart,
so additional restart and shutdown cycles remain necessary for stability.
