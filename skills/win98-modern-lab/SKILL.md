---
name: win98-modern-lab
description: Engineer the Windows 98 Shizuku's Second Edition project through Wine/ReactOS API porting, real accelerated guest and application tests, ShizukuDOS, multicore and PAE memory work, CSM hardware support, and publication. Use for work in the Win98-Modern repository, not general Windows troubleshooting.
---

# Win98 Modern Lab

Find the repository root from the current worktree and inspect its current state before acting. `README.md`, `docs/TARGET_APPS.md`, `docs/COMPATIBILITY.md`, `vm/README.md`, and the actual source and test results outrank this skill's historical details, except that the user's latest explicit target supersedes older numeric targets: **100% of the defined Windows API surface and all five selected apps actually running**. Keep the full goal intact: a new Win98 SE modernization project, direct Win98 installation and guest tests, newly built ShizukuDOS in place of FreeDOS, usable multicore execution, app-specific CPU modes, VT-x/AMD-V acceleration, PAE with all usable RAM on modern CSM systems, CSM-era boot and Skylake-class SATA/xHCI support, GitHub source, and an installable public site. Work on a concrete part without treating it as completion of the whole.

Use the relevant reference when that work begins:

- [API porting](references/api-porting.md): Wine/ReactOS source analysis, ABI, wrapper tests, and whole-API measurement.
- [Guest validation](references/guest-validation.md): the direct Win98 SE VM, test media, app probes, and evidence.
- [System work](references/system-work.md): ShizukuDOS, multicore, app CPU profiles, PAE/all usable RAM, and CSM-era AHCI/xHCI.
- [Release and site](references/release-publication.md): licenses, allowlisted packages, GitHub, and official page.

## Shared invariants

- Preserve a powered-off base VM and use independent disposable clones for risky memory, driver, or boot experiments. Confirm hardware virtualization from the **current** VM log; a VM setting alone is insufficient.
- Do not put Windows installation media, product keys, Microsoft redistributables, target-app archives, VM disks, or screenshots with keys into version control or public packages.
- Label evidence precisely: host build, PE static check, guest direct call, guest KernelEx import, app startup, and app functionality are different claims. The 100% target is the **defined whole Windows API surface** with behavioral guest validation; a curated app import match is not that score. Completion also requires all five selected applications to run and pass meaningful guest functionality checks.
- Keep source lineage and per-file licensing beside every imported algorithm. Implement real contract behavior where possible; do not count a success-shaped stub as compatible.
- Split independent research/implementation/validation among agents when useful, give them disjoint file and VM ownership, and integrate their results against the current worktree.
