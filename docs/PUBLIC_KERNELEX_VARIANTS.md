# Public KernelEx variants and extension research

Reviewed 2026-09-24. This is a source and development-history inventory for
Windows 98 SE. A forum mention, an export name, or a stub is **not** evidence
that the Windows API contract works in our installed guest. The current build
continues to use the original KernelEx 4.5.2 ABI and independently tested
extension DLLs.

| Material | What is publicly available | Use in this project |
| --- | --- | --- |
| [Original KernelEx](https://github.com/metaxor/KernelEx), pinned at [`31cdfc3`](https://github.com/metaxor/KernelEx/tree/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1) | Full 4.5.2 source; [project lists GPLv2](https://sourceforge.net/projects/kernelex/) and some files carry their own LGPL notices | Build against the real `get_api_table` ABI, inspect resolver and original implementations, preserve per-file license notices. This is the baseline already recorded in `THIRD_PARTY.md`. |
| [Kext: DIY KernelEx extensions](https://msfn.org/board/topic/157173-kext-diy-kernelex-extensions/) by jumper | Author documents `Kstub`, `Ktree`, the `iphlpapi4` extension, and sample module approach. The page describes `Kstub` as configurable stubs/forwards and says sample source is available on request; availability of each attachment and its license must be checked separately. | Use `Ktree`/`Kstub` definitions as a discovery list and compare their provider order with our `core.ini`. Implement public contracts with focused tests, not a collection of success-shaped placeholders. |
| [KernelEx 2022 / Kex22 development thread](https://msfn.org/board/topic/173233-kernelex-2022-kex22-test-versions-422262/) by jumper | Original developer reports core updates, changed resolver behavior, and a `4.5.2015.9` full-source attachment. The [2015.10 change list](https://msfn.org/board/topic/173233-kernelex-2022-kex22-test-versions-422262/page/14/) names 69 added, 37 improved and nine removed APIs, including many explicitly marked stubs. A [later forum report](https://msfn.org/board/topic/173233-kernelex-2022-kex22-test-versions-422262/page/53/) says that source attachment downloaded as zero bytes. An [independently linked Fossil mirror](https://msfn.org/board/topic/173233-kernelex-2022-kex22-test-versions-422262/page/11/) was not reachable in this review (HTTP request timed out). | Treat the authored change log as an API and regression checklist. No unverified Kex22 archive or binary is copied into this repository or the release. Reinspect source and per-file license before adapting code from a recovered archive. |
| [Later Kex22 core deltas](https://msfn.org/board/topic/173233-kernelex-2022-kex22-test-versions-422262/page/38/) | The author published `4.5.2016.18` and `.19` changes in the same thread. The `.19` list includes `TryAcquireSRWLockExclusive/Shared`, NTDLL image helpers, SetupAPI names and other exports. A [later discussion](https://msfn.org/board/topic/173233-kernelex-2022-kex22-test-versions-422262/page/39/) clarifies that these are updates atop a full 4.5.2 install, not complete replacement packages. | Compare each public change list with our actual API table and import inventory; names alone do not establish implementation quality. Preserve the original 4.5.2 guest baseline until a complete alternative source and reproducible guest regression set can be reviewed. |
| [VxKex source mirror](https://github.com/i486/VxKex) | A public Windows 7 extension tree; the reviewed revision and lack of an identified reuse grant are detailed in [our source audit](VXKEX_SOURCE_AUDIT.md). | Compare loader and API grouping only. Its NT loader and forwards do not implement those APIs on Windows 98. No VxKex code is copied. |
| [VxKex NEXT](https://github.com/YuZhouRen86/VxKex-NEXT) | Public fork of the Windows 7 extension tree with additional source and a per-application compatibility configuration. Its own README says the current supported systems are Windows 7, 8 and 8.1; the reviewed repository page does not identify a top-level reuse license. | Compare API grouping, compatibility switches and test automation as design references. Treat its Windows 7 results as unrelated to Win98 guest success. Do not copy code before checking file-level rights. |
| [One-Core-API source](https://github.com/shorthorn-project/One-Core-API-Source) | ReactOS-derived source with wrappers, additional DLLs and drivers for XP/Server 2003. The repository lists GPL-2.0, LGPL-2.1 and other licenses by file. | Study API contracts and the source layout alongside ReactOS, but evaluate each dependency against the Win9x loader and kernel. Its reported application support on NT5 is not a Win98 result. |
| [Wine](https://gitlab.winehq.org/wine/wine) and [ReactOS](https://github.com/reactos/reactos) | Public implementations with independent license and platform assumptions; exact revisions used by each port belong in `THIRD_PARTY.md`. | Analyze behavior and error paths, then adapt only what works with Win9x's ABI, synchronization, filesystem, and loader. Preserve notices for code/data actually used. |

## What the variant research changes

1. **Resolver behavior is part of compatibility.** The Kex22 author describes
   `get_api_table()` plug-in exports and a later distinction between imports
   and delay-loaded lookups in the [2015.10 discussion](https://msfn.org/board/topic/173233-kernelex-2022-kex22-test-versions-422262/page/14/).
   An exported name can fix a loader dialog while breaking a program that
   probes for the symbol at runtime. For every new API, keep separate direct,
   static-import, and real-app tests; do not count a loader pass as full
   semantic coverage.
2. **Provider order must be deliberate.** Kext's author shows different
   `contents=` orders for configurable stubs and notes their different
   priorities. Our actual guest `CORE.INI` and test DLL hashes are recorded
   per port. Before enabling any public Kstub package, inventory duplicate
   functions and route each one to exactly the tested implementation. This
   avoids a stub shadowing a working wrapper.
3. **Port candidates with observed use first.** The Kex22 change list
   suggests activation contexts, system directory, mount-point, security and
   NTDLL forwarding gaps. These are candidate names only. The current
   Notepad++ 8.9.8 loader failure and the selected app import inventory give
   higher-confidence priorities. The condition-variable, extended NLS and
   threadpool work are tracked in dedicated port documents, each with
   direct and static import guest evidence.
4. **Keep honest API accounting.** Kex22's count of new exports includes
   stubs and forwards; it cannot be added to our verified numerator. The
   denominator and guest evidence requirements remain in
   [the SDK inventory](SDK_INVENTORY.md) and [target-app policy](TARGET_APPS.md).

## Revisit criteria

If a full, readable modified KernelEx source archive becomes available, record
its URL, content hash, revision, and file-level license before comparison.
Review resolver changes and concrete implementations separately. Only then
consider a small port with its own Win98 PE gate, direct behavior test,
static-import test, and app observation. Keep the original installer and
third-party binary distribution outside the release unless their specific
redistribution terms and exact source are included.
