# Catalogue-first API porting campaign

The working unit is a **complete API family with shared prerequisites**. An
application's first loader error is regression evidence; it does not choose
the entire implementation plan. The target remains the full agreed Windows
API surface and the five selected applications, together with the separate
ShizukuDOS, hardware, memory, filesystem and multicore requirements.

## Rebuild the complete candidate backlog

```powershell
python tools/acquire_api_sources.py
python tools/index_api_sources.py
python tools/build_porting_catalog.py
python tools/query_porting_catalog.py --batch threadpool --details
python tools/query_porting_catalog.py --dll KERNEL32.DLL
```

The SDK candidate file must first exist at the path in `porting/sources.json`.
`tools/build_sdk_inventory.py` and `docs/SDK_INVENTORY.md` describe its creation.
The aggregator verifies its checksum against the committed SDK summary. It
retains **every candidate**, including COM interface identities, overloads,
WinRT contracts and API-set aliases. Native Win98 exports, current project
declarations, and additional upstream declarations are supplemental records;
their presence does not redefine the Windows API denominator.

`porting/sources.json` pins upstream commits. Source downloads are read as
archives, with checksums recorded under `build/api-sources/receipt.json`.
The upstream index preserves file, line, revision, source hash, architecture
and conditional flags, and identifies declarations, stubs, forwarders and
data. Unparsed material and unverified KernelEx variants remain explicit
discovery debt. A `.spec` or `.def` entry identifies an export location; the
implementation body and its dependency closure still need source review.
VxKex is reference metadata until a usable source license is established.

Generated artefacts under `build/api-catalog/`:

| File | Purpose |
| --- | --- |
| `upstream-exports.jsonl` | Source declaration index with exact provenance |
| `upstream-summary.json` | Parser coverage, exclusions and unresolved material |
| `catalog.jsonl.gz` | Full joined catalogue, one record per candidate identity |
| `catalog.csv` | Flat spreadsheet-readable list of all records |
| `work-queue.json` | Every record assigned to one batch, plus shared prerequisites |
| `summary.json` | Input/output hashes, counts and explicit limitations |
| `api-porting-catalog.zip` | Deterministic bundle of full catalogue, CSV, queue and summary, with separate SHA-256 file |

The committed `benchmarks/api-porting-catalog-v1.json` identifies a generated
snapshot. Rebuild after any source-table or inventory changes. Full generated
data stays under `build/`; source code, recipes and compact evidence can be
published independently of licensed Windows media and SDK header text.

## Batch execution

`porting/groups.json` defines shared foundations and initial functional groups.
Unmatched exports receive a DLL batch, COM methods an IID batch, and WinRT
methods a namespace batch. Nothing is dropped for being difficult. These
assignments are reviewable heuristics; DLL/namespace batches need subdivision
before implementation. Dependency nodes are requirements, not claimed working
subsystems.

1. List **all sibling APIs**, including A/W, Ex, begin/complete, allocation/free,
   create/close, cancel/wait and teardown operations. Record valid and invalid
   parameter behavior, ownership, ABI and thread rules.
2. Read implementations in **both Wine and ReactOS**, then relevant KernelEx,
   One-Core and licensed variants. Compare behavior against native Windows
   where useful. Check per-file licenses and source notices before reuse.
3. Trace each implementation's dependency closure. Separate portable logic
   from NT/Unix services, kernel objects, loader hooks and hardware operations.
   Implement a shared Win98 backend before adding many wrappers on top of it.
4. Give parallel agents disjoint family files, a shared ABI contract, and a
   test handoff. One integrator owns common provider tables and the guest VM.
   Avoid having several agents change loader state or the same runtime file.
5. Integrate a coherent family, with deterministic build and loader checks.
   Exercise success, failure, boundary, lifetime and concurrency cases through
   both direct calls and real static imports in the installed Win98 guest.
6. Run the five-application regression corpus at batch checkpoints. Record
   loader progression, startup and meaningful application functions separately.
   Feed failures back into their owning families without abandoning the full
   catalogue plan.

7. Have a different agent review lifetime and failure paths before publication.
   Keep its changes disjoint from the integrator. Exercise a concrete discovered
   race with a controlled fault/ordering test. Compare native behavior with
   repeated runs when scheduling can change the result: the reference test can
   contain a race too. Freeze source and test hashes before final guest runs.
8. Upgrade every route owned by the previous provider as one transaction.
   Require identical family maps in all active profiles, preserve table indices
   and unrelated configuration bytes, and reject dangling provider references.
   `remote/patch_core_wrapper_upgrade.py` discovers those routes rather than
   maintaining a stale list of individual functions. Reboot only after copying
   the versioned DLL and verified configuration, then test ordinary imports.

## One source recipe and independent lifecycle checks

`tools/m98wrap-sources.ps1` is the explicit ordered source list shared by the
checkout build, release rebuild and focused wrapper tests. Adding a backend
requires updating this one list; the release gate also requires identical
wrapper hashes beside integration tests. This prevents a newly ported family
from appearing in one build while silently missing from another.

For lifecycle families, the implementer and reviewer use separate probes.
The FLS work includes a thirteen-entry static import probe, a system MSVCRT
`_beginthreadex` routing probe, and bounded child processes for callbacks that
terminate their own threads. An API-provider's imports must remain native while
application and CRT imports route through the new backend, or recursion results.
The installed guest's IAT ownership is checked explicitly. A passing direct
fixture alone does not authorize promoting the same names into the provider.

Initial foundations cover ABI/loader, object lifetime, synchronization, thread
teardown, Unicode, file/I/O, COM, graphics, networking, security, NT services
and WinRT. Full threadpool work/timer/wait/I/O/cleanup behavior depends on these
shared foundations; adding five work-object exports does not finish the
entire threadpool family. FLS similarly requires fiber and thread teardown,
not only four storage functions.

## Completion and evidence

The catalogue records `listed` or `declared_in_project` separately from
behavioral coverage. `benchmarks/api-guest-evidence-v1.json` links focused
guest receipts for InitOnce, work/callback threadpool, SList and locale-name NLS to
the exact provider, test and source hashes. Matching snapshots are labeled
`guest_static_subset_verified`; changed or unavailable artefacts leave the
receipt historical. Other focused tests remain in family documents until
entered in the registry, and unlinked rows are `unassessed`. A declaration match, source availability, native
export, stub, forwarder, host PASS or successful DLL load is not full API
compatibility. The catalogue reports no compatibility percentage.

Further evidence linkage must pin implementation, provider and test hashes,
environment/acceleration, invocation route, tested contract subset, result,
and known limitations. A changed provider invalidates its previous binary's
receipt for the new binary. Manual import routes must also be versioned.
For an eventual whole-API percentage, first resolve aliases, architecture,
COM/WinRT identity and the agreed denominator; then use appropriate guest
behavioral evidence as the numerator. Do not remove outstanding API families
to raise the apparent rate.
