# 0.1.5-preview FLS and lifecycle checkpoint

The complete candidate catalogue remains the implementation queue. This
checkpoint adds a supported FLS/lifecycle subset and strengthens independent
review, installed routing tests, repeatable family regression and source-only
release reconstruction. The full Windows API and five-application goal is not
complete.

## Verified deliverable

- Local patch/source archive: `build/releases/win98-modern-0.1.5-preview-x86.zip`.
- Size: 765,063 bytes.
- SHA-256: `316a0ca693a5d3fc10de3ae265f401ab0997cfbc981aee77420ba26c003310eb`.
- External checksum and all 168 listed inner files passed verification.
- Independent extraction included 141 source/metadata files and excluded all
  28 shipped DLL/EXE files. The extracted `build-dlls.ps1` rebuilt every binary:
  **28 byte-identical, zero mismatch, zero missing rebuild**.
- Local machine receipts: `build/release-source-check-015-extract.json`,
  `-rebuild.json`, `-compare.json`, `-family.json`; build log is
  `build/release-source-check-015-rebuild.log`.
- The packaged all-family route helper was also tested independently: 38
  names in three profiles, 114 routes, unrelated bytes preserved.

Only explicitly selected redistributable project code, corresponding source,
notices, tests and configuration helpers are packaged. Windows media, product
keys, VM images and third-party target application packages are not included.
This is a local patch artifact; it is not an uploaded GitHub release asset.

## Directly installed Windows 98

The guest cold-booted with current VirtualBox NEM hardware acceleration.
Installed provider `M98WRP19.DLL` has 89 KERNEL32 table entries and SHA-256
`ee96a7d5dfda768f21eb0098b8dafdd4080a018847329e5e145d8086b7a46cd4`.
CORE.INI SHA-256 is
`a4a3e8e6286321a5a80a96d62bb2acc2386ba3ba384a780ef3c93f4e4c433598`.

`remote/suites/api-lifecycle.json` passed **10/10** guest tests: provider table,
FLS static imports, EXE/system-MSVCRT exit callbacks, independent FLS rundown
race, complete direct FLS fixture, SList, threadpool callback, threadpool work,
InitOnce, and NLS. Receipt: `build/guest/suite-lifecycle19-routed.json`.
EXE and MSVCRT imports selected the installed provider; the provider's own
CreateThread and ExitThread imports selected original KERNEL32. Three tested
thread exit paths each called the registered FLS callback exactly once.

Independent review reproduced and fixed callback-triggered exit abandonment
and concurrent rundown/free races. The revised deterministic regression fails
against pre-fix source and passes against current source. A suspended fiber
with a live cleanup frame is preserved and deletion returns ERROR_BUSY;
complete remote-fiber abandonment, raw/forced exit, reverse fiber conversion,
internal threadpool FLS teardown and arbitrary callback-DLL unload remain
unfinished. These tests do not establish full FLS equivalence or Win98 SMP.

## Catalogue and selected applications

The refreshed catalogue retains all 151,330 SDK candidates, 234,613 supplemental
records and 8,001 batches, with zero unassigned records. There are 123 project
declaration matches and 38 current guest contract-subset entries. The catalogue
has no whole-Windows compatibility percentage. Catalogue ZIP SHA-256:
`917594bd5ce67b668cd1c0d3c857f663158cb06ec805ccd7eb891bcc04f8ae15`.
Its CRC and inner artifact hashes passed verification.

Four selected x86 applications were attempted in the installed guest:
Notepad++ remains blocked by clipboard listeners; Chromium by vectored exception
handling; Supermium by its NT loader wrapper dependency; VLC shows only a
startup error dialog, with no playback. VSCode 1.138.0 is the selected x64
PE32+ executable and remains blocked on the x64 execution architecture path.
See `docs/TARGET_APPS.md` and `benchmarks/app-guest-checkpoint-provider19.json`.
Process launch and observer exit 0 are not counted as application success.

## Process improvements carried into the code

One explicit source recipe now feeds every wrapper build. API family routes
can be installed together with `--family all`; provider migration preserves
existing route ownership. A ten-test guest suite checks the selected versioned
agent image and records the current acceleration log. Independent agents own
implementation, adversarial tests/review and isolated release reconstruction;
one integrator owns provider tables and the guest. New app errors feed their
whole catalogue families instead of defining a one-function-at-a-time plan.
