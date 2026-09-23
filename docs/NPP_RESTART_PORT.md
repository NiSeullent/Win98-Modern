# Notepad++ application-restart API loader bridge

## Observed need and scope

After `GetLocaleInfoEx` passed a static KernelEx import in the directly
installed Windows 98 SE guest, Notepad++ 8.9.8 x86 stopped at
`KERNEL32.DLL!GetApplicationRestartSettings`
(`vm/npp-after-localeinfo.png`). Its PE imports also contain
`RegisterApplicationRestart` and `UnregisterApplicationRestart`, so the three
names are provided together. This is a loader step, **not** evidence that
Notepad++ starts.

The [Notepad++ v8.9.8 source](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/NppBigSwitch.cpp#L2286-L2388)
guards `SetOSAppRestart` with `getWinVersion() < WV_WIN10`. If the guest is
reported as older, the calls may never execute, but the static imports must
still resolve before the process can start. A compatibility mode that reports
Windows 10 may take the runtime path; the wrapper then reports the actual
absence of a restart service.

## x86 contract and Win98 behavior

The three KernelEx API-table names are exact and undecorated. Their function
pointers use `WINAPI`/x86 stdcall:

| Name | Arguments | Stack bytes | Implemented Win98 result |
| --- | --- | ---: | --- |
| `GetApplicationRestartSettings` | `HANDLE`, `PWSTR`, `PDWORD`, `PDWORD` | 16 | `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)` for a valid process with no registration |
| `RegisterApplicationRestart` | `PCWSTR`, `DWORD` | 8 | `E_FAIL` for a valid request because this build has no service that could perform a restart |
| `UnregisterApplicationRestart` | none | 0 | `S_OK` for removing a registration that cannot exist |

The `Get` wrapper rejects a null process or size pointer, and a null output
buffer paired with a nonzero capacity, with `E_INVALIDARG`. It validates a
non-null process handle using original Win98 `GetExitCodeProcess` and converts
that failure to an HRESULT. The pseudo-handle from `GetCurrentProcess()` is
valid. This handle check may not match every Windows access-right edge case;
the verified path is the current-process query used by Notepad++.

The [Microsoft `GetApplicationRestartSettings` contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getapplicationrestartsettings)
defines `pcchSize` in UTF-16 `WCHAR`s, including the terminator in the required
size. It distinguishes no registration (`0x80070490`) from invalid arguments
(`0x80070057`) and a registered command line with an insufficient buffer
(`0x8007007A`). This Win98 build cannot reach the registered-data case, so it
does not invent a command line, flags, or a successful size query. It leaves
output parameters unchanged on the no-registration path; Microsoft does not
promise those output values after an error.

The [Microsoft `RegisterApplicationRestart` contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-registerapplicationrestart)
allows a null or empty command line and documents `RESTART_MAX_CMD_LINE = 1024`
UTF-16 code units including the terminator. This wrapper accepts up to 1023
visible code units and rejects 1024 with `E_INVALIDARG`. It rejects flag bits
outside the documented `RESTART_NO_CRASH|RESTART_NO_HANG|RESTART_NO_PATCH|
RESTART_NO_REBOOT` mask (`0x0f`) with `E_INVALIDARG`. A Windows 11 host probe
accepted an unknown `0x10` bit, so that particular strict flag check follows
the published input domain but does **not** establish exact current-Windows
behavior. A valid request returns `E_FAIL`, including null/empty text; it
never claims an operational [Windows Error Reporting restart mechanism](https://learn.microsoft.com/en-us/windows/win32/recovery/registering-for-application-restart).

The [Microsoft `UnregisterApplicationRestart` contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-unregisterapplicationrestart)
returns `S_OK` when removal succeeds. Removing an absent registration is
idempotent in the tested modern Windows host and in this wrapper. There is no
process-wide registration store or automatic crash/hang/reboot recovery here.
Those behaviors remain work for a future restart supervisor and cannot be
counted as compatible from the import result alone.

## Source review and licensing

- Pinned [Wine `kernelbase/process.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/process.c#L3454-L3468)
  returns `E_NOTIMPL` for `GetApplicationRestartSettings`; its
  [KERNEL32 Register and Unregister functions](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernel32/process.c)
  return `S_OK` without a restart mechanism. Those placeholder semantics were
  not copied.
- Pinned [ReactOS `kernel32.spec`](https://raw.githubusercontent.com/reactos/reactos/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32.spec)
  lists `GetApplicationRestartSettings` and `UnregisterApplicationRestart`
  as Vista export stubs; its
  [`RegisterApplicationRestart` implementation](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32_vista/vista.c#L2055-L2075)
  returns `E_FAIL`. No ReactOS code was copied.

The three functions are new project code under GPL-2.0-only. They add no
native DLL imports. `GetExitCodeProcess` was already an import and is listed
in the pinned Korean Win98 SE OEM KERNEL32 export manifest. The static PE
gate checks every actual import against that manifest.

## Verification and guest handoff

Run `./tools/build-restart.ps1` from the repository root. The isolated build
produces `build/restart/m98wrap.dll`, `restart_smoke.exe`,
`restart_import_probe.exe`, and a 46-entry sampled `smoke.exe`.
`tests/check_restart_pe98.py` checks PE32/i386 subsystem 4.10, forbidden
loader directories and flags, Win98-native DLL imports, and exact named,
non-delay static KERNEL32 imports for all three new functions. The host
direct test checks no-registration, invalid arguments and handle, command
line lengths 1023/1024, documented flags, and repeated unregister.

On 2026-09-24 all four host gates passed. Rebuilt binary SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| `build/restart/m98wrap.dll` | `D16C988A84071F93E3A75C2602252E425917160C2F20458AAEA9FFD203DA09B4` |
| `build/restart/restart_smoke.exe` | `35E6DBBEF31F3F57B6644E45C48A4DE8C09ED916C170E2B1D6AC6B9F8DA10B00` |
| `build/restart/restart_import_probe.exe` | `FE63E5F68EFDE83F1AAB1966CBDA36C09879455D48E4348CA938AABD9D3F7966` |
| `build/restart/smoke.exe` | `CCE2AE55CC1733C2044DF629A532D88F08EDAC13A37198202CEBFBB7227EC265` |

On 2026-09-24 the direct Windows 98 guest probe (`RSTS.EXE`) returned
`PASS: application restart Win98 no-service contract` with exit code 0.
The exact DLL hash above was installed as versioned
`C:\WINDOWS\KernelEx\M98WRP9.DLL`; `CORE.INI` was changed to include
`m98wrp9` and SHA-verified after transfer. A normal guest shutdown followed
by a cold boot used the VM's WHPX/NEM hardware virtualization backend.
`RSTIMP.EXE` then returned `PASS: static KERNEL32 restart imports` and
`SMOKE46.EXE` returned `PASS: 46-entry KernelEx table and sampled API behavior`,
both with exit code 0. This checks direct calls, static imports, and sampled
regression behavior; it does not establish an operational restart service or
complete semantics for all 46 names.

The next Notepad++ 8.9.8 x86 probe still failed before startup. Its loader
dialog named `KERNEL32.DLL!QueryFullProcessImageNameW`
(`vm/npp-after-restart-apis.png`). The app has not run yet.
