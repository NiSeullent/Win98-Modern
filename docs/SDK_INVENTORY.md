# Windows SDK API candidate inventories

The project's completion target is **100% of the defined Windows API surface** against the selected Windows SDK **10.0.28000.2705**, plus execution of all five required applications. The exact 28000 C++ and x86 SDK packages have now been acquired and parsed. The [reproducible 28000 candidate summary](../benchmarks/sdk-inventory-28000-candidates-summary-v1.json) contains **151,330 distinct source-record IDs**, and [its acquisition and scope notes](API_SURFACE_28000.md) pin the two NuGet archives, input-tree hash, inventory hash, and remaining classification work. **This is a candidate record count, not the reviewed Windows API denominator, a compatibility numerator, or a measured percentage.** It is separate from application PE imports.

The locally installed SDK remains **10.0.26100.0**. Its [older provisional summary](../benchmarks/sdk-inventory-26100-provisional-summary-v1.json) is retained for a version comparison, not substituted for the selected 28000 source snapshot. The 28000 package uses internal file directories named `10.0.28000.0`; package version and verified package hashes identify the intended servicing build.

## Rebuild the older locally installed 26100 inventory

Install Python 3.10+ and the pinned WinRT metadata reader, then run from the repository root:

```powershell
python -m pip install dnfile==0.18.0
python -B tools/build_sdk_inventory.py --self-test
python -B tools/build_sdk_inventory.py `
  --sdk-root 'C:\Program Files (x86)\Windows Kits\10' `
  --sdk-version 10.0.26100.0 `
  --out build/sdk-inventory-26100-provisional-v1.json `
  --summary-out benchmarks/sdk-inventory-26100-provisional-summary-v1.json
```

The generated, ignored `build/sdk-inventory-26100-provisional-v1.json` contains individual candidate identities, source paths, and SHA-256 hashes for every input file. The committed summary contains aggregate counts, an aggregate input-tree hash, the WinMD hash, and the generated inventory hash. No SDK headers, IDL content, or WinMD bytes are committed. Output paths and local timestamps are absent from the records, so the JSON bytes are reproducible for the same SDK inputs and tool version.

## Historical 26100 results

| Candidate category | Count | Source and unit |
| --- | ---: | --- |
| Named Win32 export candidates | 30,230 | DLL name and named code export from short COFF import objects in 453 x86 UM import libraries |
| API-set export aliases | 3,929 | Same source, with `api-ms-*` or `ext-ms-*` DLL name; kept separate because names can alias host DLL exports |
| COM-style IDL method candidates | 77,029 | Method declaration under a UUID-bearing UM IDL interface or dispinterface; declared methods only |
| WinRT-style IDL method candidates | 76 | UUID-bearing IDL interfaces directly or transitively derived from `IInspectable` |
| WinRT metadata method candidates | 39,841 | Public methods on public `Windows.*` runtime types in aggregate `Windows.winmd`; overloads separate |
| **All candidate records** | **151,105** | Diagnostic sum for SDK 26100 only; semantic APIs may overlap and this is not a denominator |

The IDL parser read 544 UM IDL files and found 5,985 UUID-bearing interface definitions. The import-library parser found 97,742 named code import members before deduplicating by DLL and export name. It separately recorded 11,127 ordinal-only import members, 294 data/constant members, and 12,037 non-short import archive members that do not enter the named-code list. A lexical scan of 1,798 UM/shared headers found call-token matches for 21,437 of the 34,159 distinct named export and API-set candidates. The WinRT metadata scan found 7,086 public runtime types. These source counts are in the summary JSON.

Representative identities are `win32:KERNEL32.DLL!CreateFileW`, `com:{42f85136-db7e-439c-85f1-e4075d135fc8}::SetFileTypes#1`, and `winrt:Windows.Storage.IStorageFile::CopyAsync#1`. DLL plus name, IID plus method, or namespace plus runtime type plus method is the key. ANSI and Unicode names count separately. Repeated WinRT/IDL method names use a declaration-order suffix; that suffix must be reconciled against signatures before comparing SDK versions. Inherited interface methods are not repeated on a derived interface.

Every record has SDK version provenance through the inventory root and file-level SHA-256 source provenance. WinRT metadata records also carry `ContractVersionAttribute` name and major/minor version when present. IDL UUIDs provide interface identity. `minimum_os` and `feature_dependencies` are `null` because these sources alone do not establish them. Win32 records are specifically x86 import-library evidence; the architecture of interface methods is left `null` pending ABI validation.

## Interpretation limits

- Import libraries are linker declarations, not proof that the named export exists or works on a particular Windows build. They can contain API-set aliases, undocumented exports, and SDK-shipped components outside the eventual OS API policy. Ordinary static-library members and ordinal-only imports are not named Win32 entries here.
- The IDL scan does not evaluate preprocessor conditions. It captures UUID-bearing interface methods, but can miss dispatch-only properties, C++-only headers, and interface declarations not represented in UM IDL. Some IDL entries may duplicate WinRT metadata entries; `IInspectable` ancestry identifies only a first-pass WinRT-style subset.
- The WinRT scan reads the aggregate SDK `Windows.winmd`. A public metadata method may require a contract, capability, device, broker, package identity, or activation path. Contract version is not a direct minimum Windows version. Runtime classes, interfaces, and delegates are all included where they declare public methods.
- None of these records is a tested Windows 98 implementation. Name matching cannot establish calling convention, structure layout, ownership, errors, threading, side effects, or guest behavior. Neither the 26100 sum of 151,105 nor the 28000 sum of 151,330 may be used as a compatibility denominator.

## Work required for the fixed target

1. Classify the [28000 candidate records](API_SURFACE_28000.md) as included, alias, duplicate, out of scope, or unresolved under a published denominator policy. Resolve SDK-shipped components versus OS APIs, A/W forms, overloads, constructors, dispatch properties, ordinal-only exports, headers without import libraries, and conditional declarations. The category counts partition **record IDs**, but do not partition semantic APIs.
2. Reconcile API-set aliases with their host exports and UM IDL interfaces with WinMD by actual interface GUID and signature. The current inventory has no API-set host map or WinMD GUID/signature join; matching names alone must stay a review queue, not a subtraction from the candidate sum. Record an explicit reason and source provenance for every deduplication or exclusion.
3. Review WinRT desktop eligibility and dependencies, then annotate minimum OS, architecture, contract/interface version, feature and capability requirements, and Windows 98 guest behavior. Count a numerator entry only after tests cover the documented contract, including success and failure paths, ownership, thread behavior, and relevant edge cases.

The [application import report](PE_IMPORT_COVERAGE.md) remains a separate prioritization tool. Its match percentage has no relationship to this inventory's eventual denominator.
