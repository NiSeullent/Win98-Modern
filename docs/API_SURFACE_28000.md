# Windows SDK 10.0.28000.2705 candidate surface

This is a **fixed, reproducible source snapshot**, not the project's Windows API
compatibility denominator or numerator. The source is the exact Microsoft
Windows SDK 10.0.28000.2705 C++ NuGet release. Microsoft describes the Windows
SDK as containing Win32 and WinRT development inputs; it separately documents
that some WinRT APIs cannot be used by desktop apps or require package identity.
Therefore an aggregate SDK metadata count cannot by itself measure the requested
desktop API surface. See [Windows SDK API reference][api-reference] and
[desktop WinRT limitations][desktop-winrt].

## Pinned acquisition and reproduction

`tools/materialize_sdk_28000.py` fetches only these two Microsoft packages and
checks their entire SHA-256 and byte length before reading any member:

| NuGet package | Bytes | SHA-256 |
| --- | ---: | --- |
| [Microsoft.Windows.SDK.CPP 10.0.28000.2705][cpp] | 160,512,379 | `a74ca8f9af98bd61925d9e2932ad98f746306123167b8f0bba99cd9ea9f03807` |
| [Microsoft.Windows.SDK.CPP.x86 10.0.28000.2705][x86] | 50,624,132 | `ffbe3d04ce5a81d182eb4771e9c5e57261f5769604918b37260358df1d1f44c4` |

The common package has headers, IDL, and aggregate `Windows.winmd`; the x86
package has the UM import libraries. Microsoft's internal archive directories
are named `10.0.28000.0`. The materializer maps these bytes to the package
version `10.0.28000.2705` in an ignored working directory so the existing
inventory builder can use one version value. Package and file hashes preserve
the source identity. The script does not commit or redistribute SDK bytes.
Review the [Windows SDK license][license] before using the packages.

From the repository root with Python 3.10+:

```powershell
python -m pip install dnfile==0.18.0
python -B tools/materialize_sdk_28000.py --fetch
python -B tools/build_sdk_inventory.py --self-test
python -B tools/build_sdk_inventory.py `
  --sdk-root build/sdk-28000/materialized `
  --sdk-version 10.0.28000.2705 `
  --out build/sdk-inventory-28000-candidates-v1.json `
  --summary-out benchmarks/sdk-inventory-28000-candidates-summary-v1.json
```

The full per-record inventory stays under ignored `build/`. The committed
[summary](../benchmarks/sdk-inventory-28000-candidates-summary-v1.json) stores
counts and hashes. Reproduction must yield the same inventory SHA-256
`e95ee97b426941d3deb1d6f3f34a4f460c012f8a5623b1dc0a7e53f445b033c0`
and input-tree SHA-256
`7576ed1802901edd16a096165a32e3dd9ef771a8910672085f0ae305288be78d`.
The WinMD file SHA-256 is
`f8f32012964adb9130dd4d66fdd651daddb6cbbb9fe59e70ba6637e16e3c99ad`.
These are hashes of this tool's normalized JSON and selected input files; the
two package hashes above identify the exact downloaded NuGet archives.
An independent second invocation on the same host produced byte-for-byte
identical inventory and summary JSON files.

## Observed candidates

| Category | Records | Current unit |
| --- | ---: | --- |
| Named Win32 export candidates | 30,277 | DLL + named x86 code import |
| API-set aliases | 3,934 | API-set DLL + named x86 code import |
| COM-style IDL methods | 77,016 | UUID-bearing UM IDL interface + declared method |
| WinRT-style IDL methods | 76 | `IInspectable`-derived UM IDL declaration |
| WinRT metadata methods | 40,027 | Public `Windows.*` runtime type + public method |
| **Diagnostic sum** | **151,330** | Distinct record IDs, not distinct semantic APIs |

The parser scanned 453 x86 UM import libraries, 544 UM IDL files, 1,800 UM/shared
headers, and one aggregate WinMD. It observed 11,137 ordinal-only import
members and 293 data/constant import members outside the named-code list.
Those omissions are explicit; they are not evidence that the corresponding
interfaces are irrelevant. Compared by current provisional ID with the prior
10.0.26100.0 snapshot, 281 IDs were added and 56 removed; the change reinforces
why the older SDK must not stand in for this target.

### What overlaps in this snapshot

The five table rows **partition the 151,330 record IDs**: an ID belongs to one
category. Their relationships to actual callable operations remain unresolved.
We checked the saved per-record inventory, with these deliberately weaker
name-only comparisons:

| Comparison | Observed records | What it establishes |
| --- | ---: | --- |
| API-set alias export name also appears in a non-API-set DLL candidate | 3,202 of 3,934 alias records | A name collision; the inventory has no API-set host map, so no alias-to-host equivalence is proved |
| Non-API-set export name appears in more than one DLL | 5,231 of 30,277 records, spanning 2,580 names | DLL name is part of the record identity; identical names cannot simply be collapsed |
| UM COM-style IDL interface name + method matches a WinMD type name + method, ignoring namespace | 67 of 77,016 records | A review queue only; IID, method signature, contract, and ABI have not been joined |
| UM `IInspectable`-derived IDL interface name + method matches a WinMD type name + method | 0 of 76 records | No match under this narrow lexical key; this does not establish semantic disjointness |

The IDL split between COM-style and WinRT-style is itself a first-pass
`IInspectable` ancestry heuristic. The parser does not read a WinMD interface
GUID into its records, and its IDL overload number is declaration order rather
than a signature. Neither the 3,202 alias name matches nor the 67 interface
name matches should be subtracted from 151,330 as verified duplicates.

## Denominator freeze gates

The **100% Windows API** claim must wait for a reviewed machine-readable
denominator with stable identities and explicit dispositions. The following
issues prevent using 151,330 as that denominator:

1. **Source classification.** A short COFF import member is a link target, not
   proof of a public OS export or runtime behavior. Some UM libraries represent
   SDK-shipped or separately installed components. Header token matching is
   lexical and does not evaluate conditional compilation. APIs with no import
   library, dynamically resolved APIs, ordinal-only exports, and COM-only
   declarations need separate discovery and disposition.
2. **Alias and duplicate identity.** API-set names can alias host DLL exports.
   IDL and WinMD records can describe related interfaces, including interfaces
   that the current ancestry heuristic classifies as COM-style. A/W functions,
   overloads, inherited methods, and DLL forwarders need a written counting
   rule. Current IDL `#N` overload suffixes are declaration-order markers,
   not signature-stable IDs across SDK releases.
3. **Desktop eligibility.** `Windows.winmd` exposes types across contracts and
   app models. Some members require package identity, device capabilities, or
   UWP context. Microsoft's [desktop support list][desktop-winrt] warns it is
   not comprehensive. Each WinRT entry needs a reviewed desktop disposition
   before inclusion or exclusion. Contract version is not a desktop-support
   classification.
4. **ABI and semantics.** Architecture, minimum OS, interface/contract version,
   feature dependencies, calling convention, and behavioral test evidence need
   annotation. A name match or DLL export is not a compatible implementation.
   Native Win98 APIs also need direct guest evidence if counted in the
   numerator. Wrappers need guest behavior tests through their actual route.

Until those gates are met, report this file as the **28000 candidate inventory**
and report app import coverage separately. Do not divide implemented names by
151,330 or call the resulting fraction Windows API compatibility.

## Next bounded validation slice

1. **Deduplication pilot:** start with the 67 COM-IDL/WinMD lexical matches
   above. Decode WinMD `GuidAttribute` and `MethodDef` signatures from the
   hash-pinned `Windows.winmd`; compare IID and full ABI signature with the
   declarations in the hash-pinned UM IDL files. Emit a review row for every
   match and mismatch with both source-relative paths, file SHA-256, and a
   decision reason. This pilot must also show whether the COM/WinRT ancestry
   heuristic misclassified a matching interface. Keep all other candidates
   unresolved; a matching method name alone is not a deduplication decision.
2. **Desktop eligibility pilot:** take the 12 declared method records of
   `Windows.Storage.IStorageFile` found in this WinMD, including property
   accessors; expand their overload
   signatures and inherited interfaces, then record per-method desktop,
   package-identity, capability, and contract evidence from the
   [versioned Microsoft API page][istoragefile] and
   [desktop WinRT support guidance][desktop-winrt]. Preserve the WinMD hash and
   method token/signature beside each decision. An absent entry in Microsoft's
   non-comprehensive restriction list is not enough to mark a method supported.
   Do not infer Windows 98 guest compatibility from this classification.

[cpp]: https://www.nuget.org/packages/Microsoft.Windows.SDK.CPP/10.0.28000.2705
[x86]: https://www.nuget.org/packages/Microsoft.Windows.SDK.CPP.x86/10.0.28000.2705
[license]: https://aka.ms/WinSDKLicenseURL
[api-reference]: https://learn.microsoft.com/en-us/windows/apps/api-reference/
[desktop-winrt]: https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/winrt-api-desktop-app-support
[istoragefile]: https://learn.microsoft.com/en-us/uwp/api/windows.storage.istoragefile?view=winrt-28000
