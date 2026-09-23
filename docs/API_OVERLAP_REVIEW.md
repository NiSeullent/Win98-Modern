# SDK 28000 IDL / WinMD lexical overlap review

This is a bounded review of the 67 COM-style IDL method records whose
unqualified interface name and method name also occur in the WinMD candidate
inventory. It does not freeze a Windows API denominator, remove inventory
records, or establish Windows 98 compatibility. The candidate inventory and
its limitations are described in [API_SURFACE_28000.md](API_SURFACE_28000.md).

## Sources and procedure

The input is the locally materialized Microsoft Windows SDK C++ packages at
version `10.0.28000.2705`. The pinned inventory is
`build/sdk-inventory-28000-candidates-v1.json` (SHA-256
`e95ee97b426941d3deb1d6f3f34a4f460c012f8a5623b1dc0a7e53f445b033c0`),
with input-tree SHA-256
`7576ed1802901edd16a096165a32e3dd9ef771a8910672085f0ae305288be78d`.
The review checks those hashes against the committed inventory summary before
reading the source files. The source-relative files used for these 67 rows are:

| Pinned SDK source | SHA-256 |
| --- | --- |
| `UnionMetadata/10.0.28000.2705/Windows.winmd` | `f8f32012964adb9130dd4d66fdd651daddb6cbbb9fe59e70ba6637e16e3c99ad` |
| `Include/10.0.28000.2705/um/UIAutomationCore.idl` | `57833474568fcdc6ea385834d95b7f7d6229c6949c20902d714a5ea16b5297eb` |
| `Include/10.0.28000.2705/um/wuapi.idl` | `ba0e980fe970fc4ec24b56ff3a365a6c1f25e8a70366178d434b92364ca0ff9b` |

[`tools/review_sdk_overlaps.py`](../tools/review_sdk_overlaps.py) reconstructs
the 67 name pairs from the pinned inventory. For each pair it finds the UUID
and method declaration in the source IDL, the WinMD type's `GuidAttribute`, and
the matching public `MethodDef` signature blob. The machine-readable
[`sdk-overlap-review-v1.json`](../benchmarks/sdk-overlap-review-v1.json) retains
all 67 pairs, source-relative paths and hashes, declaration text, WinMD method
token and signature, identity disposition, and a reason. Its SHA-256 after this
review is `c47133e9712c1586e44d17ee9ed339ab5276498f2936c73a2ef72d8705ab744b`.

To reproduce from the repository root after materializing the SDK as described
in the candidate-inventory document:

```powershell
python -B tools/review_sdk_overlaps.py --self-test
python -B tools/review_sdk_overlaps.py
```

## Result

| Disposition within this lexical queue | Method pairs | Meaning |
| --- | ---: | --- |
| Different IDL IID and WinMD `GuidAttribute` | 65 | Distinct nominal interface identities; the shared name does not establish an API duplicate. |
| Same nominal IID, ABI/semantics unresolved | 2 | Both methods belong to `ITextEditProvider`; no safe deduplication decision follows from the projected signature. |
| Confirmed semantic duplicates | 0 | None established by this review. |

All 67 IDL declarations were recovered from their pinned source files. All 67
WinMD owners in the pinned inventory are interface types. All 67
WinMD method signature blobs were decoded by the supported-element parser,
including the generic-instance argument of the unrelated
`IUpdateInstaller::Install` name match. A decoded WinMD signature is a metadata
projection; it is not by itself a complete binary ABI comparison with an IDL
vtable method.

The two unresolved rows are
`ITextEditProvider::GetActiveComposition` (`MethodDef 0x0600cee2`) and
`ITextEditProvider::GetConversionTarget` (`MethodDef 0x0600cee3`). In
`UIAutomationCore.idl`, both are declared as `HRESULT` methods with one
`[out, retval] ITextRangeProvider **` argument. WinMD projects each as a
zero-argument method returning
`Windows.UI.Xaml.Automation.Provider.ITextRangeProvider`; both blobs are
`200012c000b521`. That `HRESULT`/retval versus projected-return difference
alone cannot decide whether the binary calling sequence is the same.

The owner IID is identical in both sources:
`ea3605b4-3a05-400e-b5f9-4e91b40f6176`. The referenced interfaces do not
have identical GUIDs. IDL `ITextEditProvider` inherits `ITextProvider` IID
`3589c92c-63f3-4367-99bb-ada653b77cf2`; WinMD's inherited
`Windows.UI.Xaml.Automation.Provider.ITextProvider` has GUID
`db5bbc9f-4807-4f2a-8678-1b13f3c60e22`. IDL's returned
`ITextRangeProvider` has IID `5347ad7b-c355-46f8-aff5-909033582f63`;
the WinMD projected return interface has GUID
`0274688d-06e9-4f66-9446-28a5be98fbd0`. These discrepancies make a
name-and-owner-GUID deduplication unsafe. The complete inherited vtable layout,
return-interface mapping, and runtime meaning require further review before
either pair can be classified as the same callable operation or as a distinct
operation despite the reused owner IID.

The IDL ancestry heuristic did not place these two methods in the COM-style
queue by mistake: the source declares `ITextEditProvider : ITextProvider`, and
`ITextProvider : IUnknown`. WinMD separately marks its namesake as a Windows
Runtime interface. Thus the queue reveals a cross-model name/IID collision,
not evidence that the inventory's COM and WinRT method rows are disjoint.

## Boundary of this decision

The 65 different-GUID decisions address nominal interface identity only. They
do not prove that similarly named operations are behaviorally unrelated. The
two matching-GUID rows remain unresolved; no record is subtracted from the
151,330 diagnostic candidate sum. This review says nothing about API-set
aliases, exports, desktop eligibility, inherited method counting outside this
queue, or guest execution. A denominator decision needs explicit counting
rules and the remaining source and behavioral evidence described in
[API_SURFACE_28000.md](API_SURFACE_28000.md).
