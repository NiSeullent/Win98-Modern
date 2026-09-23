# Notepad++ 8.9.8: Shell import analysis and guest result

This began as a read-only review of `benchmarks/media/npp-8.9.8/notepad++.exe` (x86 PE32, SHA-256 `960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78`), the [Windows 98 SE OEM export manifest](../benchmarks/win98se-ko-oem-native-exports-v1.json), the [pinned KernelEx source declarations](../benchmarks/kernelex-source-declarations-v1.json), and the [m98shell implementation](../src/m98shell.c). The three Shell names now pass focused direct and static-import tests in the Windows 98 guest; see [the integration record](SHELL_ITEM_PORT_DRAFT.md). Notepad++ itself then reached the `UXTHEME.DLL!DrawThemeTextEx` loader error and has not started.

## Direct Shell imports

The EXE imports 12 `SHELL32.DLL` entries by the ordinary import table and none by the delay-import table. The OEM manifest contains the eight other entries: `SHFileOperationW`, `Shell_NotifyIconW`, ordinal `#165`, `ShellExecuteW`, `DragFinish`, `DragQueryPoint`, `DragQueryFileW`, and `ShellExecuteExW`.

| Absent from OEM `SHELL32.DLL` | Current source route | Remaining issue |
| --- | --- | --- |
| `SHCreateItemFromParsingName` | `m98shell` implements it; the three-export build passed the guest direct and static-import probes. | Focused file-system behavior only. |
| `SHParseDisplayName` | `m98shell` implements it and `integration/core.ini` maps the name to `m98shell.0`. | Guest direct and static-import probes passed. |
| `SHGetFolderPathW` | KernelEx `kexbasen` forwards to `SHELL32.DLL` or `SHFOLDER.DLL`; the OEM manifest lists `SHFOLDER.DLL!SHGetFolderPathW`. | A source/name match only. Confirm its actual return and path when the app reaches the call. |
| `SHOpenFolderAndSelectItems` | KernelEx `kexbases` advertises the name but routes it to `SHOpenFolderAndSelectItems_stub` (`UNIMPL_FUNC`). The new `m98shell_openfolder.c` handles the Notepad++ `cidl == 0`, flags-zero file-system case with native Explorer `/select`. | Guest direct test and static import passed; `vm/select-shell3-guest.png` shows the selected test file. Broader namespace/array support is still missing. |

The four bundled plugin DLLs have no `SHELL32.DLL` import. The separate updater `updater/GUP.exe` imports `SHCreateDirectoryExW` and `SHGetFolderPathW`, both declared by KernelEx; it is not the Notepad++ main EXE.

## Why `SHOpenFolderAndSelectItems` is next

Disassembly of the exact EXE shows one call at VA `0x45D0B3` to `SHParseDisplayName(path, NULL, &pidl, 0, NULL)`. On success it immediately calls `SHOpenFolderAndSelectItems(pidl, 0, NULL, 0)` at VA `0x45D0C8`, saves that `HRESULT`, and frees the PIDL. This is a concrete runtime dependency, though it need not execute during startup. The other new call, at VA `0x460767`, passes IID `43826d1e-e718-42ee-bc55-a1e261c37bfe` (`IShellItem`) to `SHCreateItemFromParsingName`; its success path invokes vtable slot `0x10`, `IShellItem::GetParent`, which the current `m98shell` implements. These call-site facts were checked against the import address table and GUID bytes in the executable; they are not claims about guest execution.

The [Microsoft contract](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenfolderandselectitems) takes an absolute folder PIDL, a child count and child PIDL array, and flags, returning `S_OK` only after opening the folder and selecting the requested item(s). With `cidl == 0`, as Notepad++ passes, `pidlFolder` instead names the **item**, and its parent must open with that item selected. COM initialization is required. A no-op `S_OK` would violate this contract.

The pinned [Wine implementation](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/shell32/shlfolder.c) splits a `cidl == 0` PIDL into parent and child, finds or opens an Explorer window, then sends selection data to it. The pinned [ReactOS implementation](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/shell32/shlfolder.cpp) handles only a single selected item by constructing an `explorer.exe /select,` request; its own source labels this a limited implementation. A Win98 port can start with the exact `cidl == 0`, `flags == 0` filesystem case exercised by Notepad++, but must verify Explorer selection in the guest and return a failure for unsupported namespace items, multiple selections, or flags. An Explorer process launch alone is insufficient proof of selection.

## Current `m98shell` contract review

- `SHCreateItemFromParsingName` has four Win32 `HRESULT` arguments, and `SHParseDisplayName` has five. The EXE call sites push those counts. The named API table is sorted and registered for `SHELL32.DLL`; both names and the four-argument selection function passed guest static-import and direct probes.
- The pinned [Wine item creation path](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/shell32/shellitem.c) and [ReactOS path](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/shell32/CShellItem.cpp) parse to a PIDL, construct an item, and query the requested IID. The wrapper does the same for `IUnknown` and `IShellItem`. NPP's encoded IID is the supported `IShellItem` IID, and its observed method slot is supported.
- The [public parse contract](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shparsedisplayname) requires the caller to own the returned PIDL. The wrapper clears outputs on failure, duplicates the const input before the native `IShellFolder::ParseDisplayName` call, forwards `IBindCtx`, and masks optional output attributes to requested bits. These paths passed direct host and guest smokes.
- The item implements the COM task allocator for returned names, reference ownership, `GetParent`, attributes, names, comparison with zero hint, and three handlers. It deliberately rejects other IIDs such as `IShellItem2` and unsupported handlers. Filename conversion still goes through the guest ANSI code page and `SHGetPathFromIDListA` for filesystem paths, so nonrepresentable or long paths remain limitations. These are runtime compatibility boundaries, not additional EXE loader imports.

No further missing `SHELL32.DLL` name can be identified from this EXE's direct import table after the two `m98shell` names are routed. The next loader dialog, if any, must be recorded from the guest; PE name analysis cannot predict its exact order or calls made through `GetProcAddress`/COM.
