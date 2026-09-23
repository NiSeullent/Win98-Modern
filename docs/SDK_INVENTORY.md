# Provisional Windows SDK API inventory

The project's completion target is **100% of the Windows API surface** against the frozen Windows SDK **10.0.28000.2705**, plus execution of all five required applications. The local machine currently has SDK **10.0.26100.0**. [The reproducible 26100 summary](../benchmarks/sdk-inventory-26100-provisional-summary-v1.json) is a first pass over that older SDK, **not** the 28000 denominator and **not** a measured compatibility percentage. It is also not a count of application PE imports.

## Rebuild the local inventory

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

## What the 26100 pass found

| Candidate category | Count | Source and unit |
| --- | ---: | --- |
| Named Win32 export candidates | 30,230 | DLL name and named code export from short COFF import objects in 453 x86 UM import libraries |
| API-set export aliases | 3,929 | Same source, with `api-ms-*` or `ext-ms-*` DLL name; kept separate because names can alias host DLL exports |
| COM-style IDL method candidates | 77,029 | Method declaration under a UUID-bearing UM IDL interface or dispinterface; declared methods only |
| WinRT-style IDL method candidates | 76 | UUID-bearing IDL interfaces directly or transitively derived from `IInspectable` |
| WinRT metadata method candidates | 39,841 | Public methods on public `Windows.*` runtime types in aggregate `Windows.winmd`; overloads separate |
| **All candidate records** | **151,105** | Diagnostic sum only; categories can overlap and are not yet an API denominator |

The IDL parser read 544 UM IDL files and found 5,985 UUID-bearing interface definitions. The import-library parser found 97,742 named code import members before deduplicating by DLL and export name. It separately recorded 11,127 ordinal-only import members, 294 data/constant members, and 12,037 non-short import archive members that do not enter the named-code list. A lexical scan of 1,798 UM/shared headers found call-token matches for 21,437 of the 34,159 distinct named export and API-set candidates. The WinRT metadata scan found 7,086 public runtime types. These source counts are in the summary JSON.

Representative identities are `win32:KERNEL32.DLL!CreateFileW`, `com:{42f85136-db7e-439c-85f1-e4075d135fc8}::SetFileTypes#1`, and `winrt:Windows.Storage.IStorageFile::CopyAsync#1`. DLL plus name, IID plus method, or namespace plus runtime type plus method is the key. ANSI and Unicode names count separately. Repeated WinRT/IDL method names use a declaration-order suffix; that suffix must be reconciled against signatures before comparing SDK versions. Inherited interface methods are not repeated on a derived interface.

Every record has SDK version provenance through the inventory root and file-level SHA-256 source provenance. WinRT metadata records also carry `ContractVersionAttribute` name and major/minor version when present. IDL UUIDs provide interface identity. `minimum_os` and `feature_dependencies` are `null` because these sources alone do not establish them. Win32 records are specifically x86 import-library evidence; the architecture of interface methods is left `null` pending ABI validation.

## Interpretation limits

- Import libraries are linker declarations, not proof that the named export exists or works on a particular Windows build. They can contain API-set aliases, undocumented exports, and SDK-shipped components outside the eventual OS API policy. Ordinary static-library members and ordinal-only imports are not named Win32 entries here.
- The IDL scan does not evaluate preprocessor conditions. It captures UUID-bearing interface methods, but can miss dispatch-only properties, C++-only headers, and interface declarations not represented in UM IDL. Some IDL entries may duplicate WinRT metadata entries; `IInspectable` ancestry identifies only a first-pass WinRT-style subset.
- The WinRT scan reads the aggregate SDK `Windows.winmd`. A public metadata method may require a contract, capability, device, broker, package identity, or activation path. Contract version is not a direct minimum Windows version. Runtime classes, interfaces, and delegates are all included where they declare public methods.
- None of these records is a tested Windows 98 implementation. Name matching cannot establish calling convention, structure layout, ownership, errors, threading, side effects, or guest behavior. The 151,105 sum must not be divided into a compatibility numerator or presented as progress toward 100%.

## Work required for the fixed target

1. Install or obtain Windows SDK **10.0.28000.2705**, run this tool against that exact version with version-specific `--out` and `--summary-out` paths, and compare file hashes and category changes. The 26100 entries are not assumed to all survive in 28000.
2. Define the denominator policy for OS APIs versus SDK-shipped components, API-set aliases, A/W forms, overloads, constructors, dispatch properties, ordinal-only exports, headers without import libraries, and declarations behind conditions. Reconcile duplicate WinRT IDL and metadata identities and add signature-aware stable IDs.
3. Annotate minimum OS, architecture, contract/interface version, feature and capability dependencies, and tested Windows 98 behavior. Count a numerator entry only after guest tests cover the documented contract, including success and failure paths, ownership, thread behavior, and relevant edge cases.

The [application import report](PE_IMPORT_COVERAGE.md) remains a separate prioritization tool. Its match percentage has no relationship to this inventory's eventual denominator.
