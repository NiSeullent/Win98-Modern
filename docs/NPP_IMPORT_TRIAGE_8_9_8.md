# Notepad++ 8.9.8 x86 import triage

The pinned `benchmarks/media/npp-8.9.8/notepad++.exe` has SHA-256
`960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78`.
Its PE import table has 614 ordinary imports and no delay imports. A source-name
review on 2026-09-23 compared those entries with the Korean Windows 98 SE OEM
export manifest, pinned KernelEx source declarations, and the project's API
tables and DEF files. At that snapshot, 537 names were in the OEM exports,
14 were KernelEx source candidates, 34 had project wrapper names, and 29 had
no matching implementation name. These are mutually exclusive triage buckets
for the EXE's direct imports. A declaration or name match does not prove that
KernelEx resolves the symbol in the installed guest or that the function works.

## Unmatched direct EXE imports in that snapshot

- `KERNEL32.DLL`: `GetProductInfo`, `GetTimeFormatEx`, `GetDateFormatEx`,
  `GetFinalPathNameByHandleW`, `FindFirstStreamW`, `GetLocaleInfoEx`,
  `GetApplicationRestartSettings`, `UnregisterApplicationRestart`,
  `QueryFullProcessImageNameW`, `RegisterApplicationRestart`,
  `SleepConditionVariableSRW`, `WakeAllConditionVariable`,
  `CloseThreadpoolWork`, `SubmitThreadpoolWork`, `CreateThreadpoolWork`,
  `FreeLibraryWhenCallbackReturns`, `InitOnceBeginInitialize`,
  `InitOnceComplete`, `LCMapStringEx`, `CompareStringEx`, `FlsAlloc`,
  `FlsGetValue`, `FlsSetValue`, `FlsFree`.
- `ADVAPI32.DLL`: `RegGetValueW`.
- `USER32.DLL`: `AddClipboardFormatListener`,
  `RemoveClipboardFormatListener`.
- `COMCTL32.DLL`: ordinals `#345` and `#381` are absent from the OEM
  manifest. In two inspected local x86 Common Controls 6.0 builds these are
  `TaskDialogIndirect` and `LoadIconWithScaleDown`. That version-specific
  observation does not define a stable ordinal contract for every build.

Five locale-name functions can share a carefully tested name-to-LCID and
Unicode conversion path: `GetTimeFormatEx`, `GetDateFormatEx`,
`GetLocaleInfoEx`, `LCMapStringEx`, and `CompareStringEx`. The four FLS names
also recur in the bundled `nppPluginList.dll`, `mimeTools.dll`, and
`NppConverter.dll`. Correct FLS support needs fiber-local storage and exit
callbacks; a TLS alias would give wrong behavior. Condition-variable,
threadpool-work, restart-registration, path/stream, and InitOnce names form
other implementation groups. These groupings prioritize source review and
tests; they are not claims about the next loader failure.

The separate `updater/GUP.exe` has its own import gaps, including NCRYPT
`BCrypt*` names, and is not an ordinary import dependency of the main EXE.
The three plugin DLLs are likewise separate binaries, so their missing names
must not be counted as the EXE's initial loader error. Subsequent guest
patches advanced the main EXE through the time/date, product, path, stream,
locale-name, and application-restart imports. The latest observed loader error is
`KERNEL32.DLL!InitializeSListHead`
(`build/guest/npp-after-initonce16.png`) after the versioned process-path, SRW
condition-variable, NLS, threadpool-work and InitOnce families passed their
focused guest probes. The full SList family is retained in the complete
catalogue; this app error is a batch regression result. Runtime calls, child processes, COM and
`GetProcAddress` dependencies remain outside this direct-import count.
