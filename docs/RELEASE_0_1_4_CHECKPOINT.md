# 0.1.4-preview API family checkpoint

This checkpoint improves the catalogue-first development workflow and adds
SList and threadpool callback families. It does not complete the full Windows
API or required application target.

## Artifacts and verification

- Local allowlisted patch/source archive:
  `build/releases/win98-modern-0.1.4-preview-x86.zip`, 670,330 bytes.
- Archive SHA-256:
  `b33de65c57b0b6ae44d90994359264d6addac01fe2f3c3484f04f248c996ba68`.
- External checksum and all 141 internally listed files passed verification.
- An independent agent extracted only the 121 source/metadata members into a
  new directory, excluding all 21 DLL/EXE members. It rebuilt there and matched
  **all 21 binaries byte for byte**. No original project build file was changed.
- Detailed local records: `build/release-source-check-014-rebuild.log`,
  `build/release-source-check-014-extract.json`, and
  `build/release-source-check-014-compare.json`.

The package contains redistributable project code, corresponding source,
notices and bounded tests. It does not contain Windows installation media,
VM images, product keys, target-app packages, or a production FLS provider.

## Directly installed Win98 guest

The hardware-accelerated `Win98Modern-Accel-128` guest cold-booted with
`M98WRP18.DLL`, SHA-256
`0da854a1a6ff42296e2e5c7fda92e07a660351a5b14b568f18c2846c609c2f3e`.
The installed CORE.INI SHA-256 is
`acbe6e697f370e48361fa86529d53865c9f3adfedd9ca366ed120c545b182290`.
All 75 explicit family routes (25 names across three profiles) select that
provider. Its sorted KERNEL32 API table contains 76 names.

Five static-import suites passed: InitOnce 4, work threadpool 5, callback
threadpool 7, SList 7, and NLS 2. These **25 focused contract subsets** have
exact source/provider/test/supporting-DLL hashes in the evidence registry.
The 76-name table smoke also passed. Direct SList fault recovery, the held-lock
process-termination regression and the reviewed FLS fixture passed separately.

Notepad++ 8.9.8 still fails to start, now at `KERNEL32.FlsAlloc`. FLS needs
core lifecycle routing and reentrant thread-termination work before production
integration. No new success is claimed for the other four required apps or
for the separate SMP/PAE/hardware/filesystem/prebuilt workstreams.

## Workflow changes

The complete candidate catalogue remains the work queue. Independent review
now checks lifetime, reentry, the reference test itself, provider-route
migration and package reproducibility. A native test's DLL-unload timing race
was reproduced and fixed with a real detach notification. Batch state reports
partial guest evidence or stale evidence separately from unreviewed exports;
no partial evidence state becomes full compatibility.
