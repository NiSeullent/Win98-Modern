# GetTimeFormatEx Win98 port

## Why this export

Notepad++ 8.9.8 reached a `KERNEL32.DLL!GetTimeFormatEx` loader error after
the UXTHEME dependency was resolved in the installed Windows 98 SE guest.
That is a loader observation, not an application startup result.

The six-argument `GetTimeFormatEx` ABI takes a Unicode locale name and
returns a UTF-16 time string. Windows 98's native `GetTimeFormatA` takes an
LCID and returns ANSI bytes. The implementation in `src/m98wrap.c`:

1. Resolves `NULL`, the invariant name, the system-default sentinel, and
   installed ISO language/region names. It searches native Win98 locale
   metadata with `EnumSystemLocalesA` and `GetLocaleInfoA`; unavailable
   names fail with `ERROR_INVALID_PARAMETER`.
2. Passes time-picture semantics, user preferences, and documented time flags
   to the native Win98 formatter. It checks invalid flags and time fields.
3. Converts custom picture and result using the selected locale's ANSI code
   page, then calculates the output size in UTF-16 code units, including NUL.
   It returns `ERROR_INSUFFICIENT_BUFFER` without writing a short buffer.
4. Protects the synchronous Win98 locale enumeration callback context with a
   process-wide critical section. The API has no caller-context argument.

The direct OEM export manifest confirms every new native import in
`KERNEL32.DLL` from the supplied Korean Win98 SE installation media. The
static gate rejects other DLL imports and Windows 98-incompatible PE flags.

## Source review and lineage

- [Microsoft `GetTimeFormatEx`](https://learn.microsoft.com/en-us/windows/win32/api/datetimeapi/nf-datetimeapi-gettimeformatex) defines the ABI, flags, length query, and error cases. [Microsoft `GetLocaleInfoA`](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getlocaleinfoa) documents locale ANSI code-page conversion; [Microsoft `EnumSystemLocalesA`](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumsystemlocalesa) documents synchronous LCID enumeration.
- Pinned Wine [`dlls/kernelbase/locale.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/locale.c) resolves the locale name against an NT NLS table, validates buffer arguments, and calls its Unicode time formatter.
- Pinned ReactOS [`dll/win32/kernel32/winnls/string/lcformat.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/winnls/string/lcformat.c) resolves the name through `LocaleNameToLCID` and calls the Unicode formatter. Its [locale-name conversion](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32_vista/LocaleNameToLCID.c) depends on RTL locale tables unavailable on Win98.
- Pinned KernelEx [`unikernel32.c`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbasen/kernel32/unikernel32.c) already bridges `GetTimeFormatW` through ANSI, but sizes its ANSI allocation with a UTF-16 character count. This port allocates by actual ANSI byte count and returns the UTF-16 count.

No Wine or ReactOS code was copied. The locale enumeration, conversion, and
error handling are original GPL-2.0-only code for Win98's native NLS calls.

## Verified on host

Run `./tools/build-timeformat.ps1` from the repository root. It builds the
Win98-targeted DLL and two probes under `build/timeformat/`. The host run has:

- PE32/i386 subsystem 4.10 and original-OEM import gate: PASS.
- Direct API-table behavior: PASS for `en-US`, neutral `en`, `ko-KR`, UTF-16
  sizing of Korean DBCS output, invariant and default names, 24-hour and
  no-seconds flags, local-time fallback, buffer limits, invalid time, locale,
  and flags.
- Static `KERNEL32.GetTimeFormatEx` import probe on the host: PASS. Its PE
  import table contains `GetTimeFormatEx` and only `KERNEL32.DLL`.
- Existing 38-wrapper host smoke: PASS after updating the API table count.

Host success does not establish Win98 guest behavior. The static import probe
uses the host's native KERNEL32 when run there; it uses KernelEx only in the
installed guest.

## Guest integration test still required

The parent workflow owns the active VM and `integration/core.ini`. Deploy the
new DLL with a versioned KernelEx provider name if the prior `m98wrap.dll` is
loaded. Add that provider to `DCFG1 contents` and route
`KERNEL32.GetTimeFormatEx` to it, restart the GUI normally, and run these
two PE32 executables in the actual guest:

- `build/timeformat/timeformat_smoke.exe` with the new DLL beside it. This
  calls the provider directly and checks return values and errors.
- `build/timeformat/timeformat_import_probe.exe`. This requires KernelEx to
  resolve the static `KERNEL32.GetTimeFormatEx` import before `main` runs.

Then retry the pinned Notepad++ 8.9.8 loader/GUI probe. Report the next
loader error or actual visible app behavior; a resolved import alone is not
an app startup pass.

## Known limits

- Win98's NLS database predates modern supplemental and custom locale names.
  This wrapper rejects names absent from that database; it does not invent
  locale mappings. Script-qualified names are rejected because Win98's ISO
  metadata cannot distinguish the requested script.
- A custom UTF-16 time picture containing characters not encodable in the
  locale ANSI code page fails with `ERROR_NO_UNICODE_TRANSLATION`. The native
  Windows Vista Unicode formatter can preserve such literals.
- The invariant locale uses an explicit `HH:mm:ss` picture with Win98 en-US
  time marker data. This does not cover every invariant time-marker edge case.
- Actual Win98 NLS behavior, KernelEx routing, and application use remain
  unverified until the guest tests above pass. This export must not be counted
  as whole-API compatible solely from these host results.
