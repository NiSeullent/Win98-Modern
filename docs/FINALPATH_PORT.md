# GetFinalPathNameByHandleW Windows 98 port

## Scope

After the Windows 98 SE guest passed the `GetDateFormatEx` static import probe,
Notepad++ 8.9.8 reported `KERNEL32.DLL!GetFinalPathNameByHandleW` as its next
missing loader import. This port adds the four-argument ABI to the sorted
KernelEx KERNEL32 table. Supplying a loader symbol does not establish that
Notepad++ starts or that the Windows API surface is fully compatible.

Win98 offers no general native operation that retrieves a final path from an
arbitrary file handle. This wrapper returns a path only when the handle refers
to the current process image. It obtains that path from `GetModuleFileNameA`,
expands its long components with `GetLongPathNameA`, and opens it independently.
Both open handles must report the same **nonzero** volume serial and 64-bit
file index, one link, file size, and creation time. The returned path is a
verified identity match, rather than a guess based on a name or file contents.

For that subset, flags 0 return the `\\?\C:\...` DOS-volume spelling;
`VOLUME_NAME_NONE` (4) returns the root-relative `\...` spelling. A successful
result excludes the terminating NUL from its length. An undersized or zero
capacity returns the required length **including** NUL, sets
`ERROR_INSUFFICIENT_BUFFER`, and leaves the caller's buffer untouched.
`FILE_NAME_OPENED` (8), `VOLUME_NAME_GUID` (1), and `VOLUME_NAME_NT` (2) fail
with `ERROR_NOT_SUPPORTED`. Invalid or combined volume flags fail with
`ERROR_INVALID_PARAMETER`. Other disk files, directories, pipes, and handles
whose identity cannot be verified fail explicitly. The wrapper does not scan
the filesystem or return the process image name for an unrelated handle.

### FAT32 file identity gate

Microsoft documents volume serial plus file index as the way to compare two
open handles, and says FAT reports one link. That is the basis for the image
identity check; it is **not** proof that the installed Windows 98 SE FAT32
implementation supplies nonzero file indices in this guest. If both index
parts are zero, this wrapper returns `ERROR_NOT_SUPPORTED`, even for the
process image. A zero or missing ID cannot be replaced safely by matching
only size, timestamps, or bytes: distinct files could share those values.

`tests/finalpath_smoke.c` prints `IMAGE_ID serial=... index=... links=...`
before calling the wrapper. The guest direct test will establish whether the
Win98 FAT32 file ID is usable. If it prints `index=00000000:00000000`, that
explains an expected explicit failure and requires a separately validated
handle-to-path mechanism before expanding the supported set. The host result
does not settle this guest-specific question.

## Source review and licensing

- [Microsoft `GetFinalPathNameByHandleW`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew) defines the ABI, flags, output spelling, and length contract. [Microsoft `BY_HANDLE_FILE_INFORMATION`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information) defines the file identity and FAT link-count fields; [`GetFileInformationByHandle`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle) notes that network backends may return partial information.
- Pinned [Wine `dlls/kernelbase/file.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/file.c) uses NT object-name queries and volume mappings for this API. Those facilities are absent from Windows 98.
- Pinned [ReactOS `kernel32_vista/GetFinalPathNameByHandle.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32_vista/GetFinalPathNameByHandle.c) queries NT file/object names and the mount manager; its source header marks that file MIT. This approach also requires an NT kernel.
- Pinned KernelEx `31cdfc3560fc116637ee8ed7be31b12f3aacf5d1` provides the sorted API-table mechanism but no `GetFinalPathNameByHandleW` implementation or general handle-to-name primitive.

The implementation is independent project code under the repository's
GPL-2.0-only license; no Wine or ReactOS code was copied. The native calls
newly needed by this port (`CloseHandle`, `CreateFileA`, `GetFileType`,
`GetLongPathNameA`, `GetModuleFileNameA`, and
`GetFileInformationByHandle`) are present in the pinned Korean Windows 98 SE
OEM KERNEL32 export manifest. `MultiByteToWideChar` was already imported.

## Verification and guest handoff

Run `./tools/build-finalpath.ps1` from the repository root. It builds
`build/finalpath/m98wrap.dll`, a direct API-table smoke test, and a static
KERNEL32 import probe. The PE gate checks the linked wrapper against the
Win98 native-export manifest, verifies PE32/i386 loader flags, and confirms
that the probe statically imports the new name. The direct host test checks
the process-image subset, volume spellings, buffer sizing, unsupported flags,
invalid handles, and rejection of an unrelated file. The static probe runs
against the host's native KERNEL32, proving import shape and host behavior
only. The 41-entry API-table smoke verifies sorted-table loading.

The host build passed the PE gate, direct API-table test, static import probe,
and 41-entry smoke test. Its `m98wrap.dll` SHA-256 is
`1f56bdc378cc01b84b6174fd300e1aa3914ab1a80455c987c702791079b6299b`.
The isolated build disables PE timestamps; a second build reproduced that hash.

On 2026-09-24 the final exact DLL above was transferred with a verified SHA-256
to the directly installed Windows 98 SE guest, selected in `CORE.INI` as
`M98WRP6.DLL`, and cold-booted to the GUI under VirtualBox's current WHPX/NEM
hardware backend. The direct smoke printed
`IMAGE_ID serial=307A16F4 index=0004FF69:00000053 links=00000001` and passed
the verified process-image subset with exit 0. The static
`KERNEL32.GetFinalPathNameByHandleW` import probe passed with exit 0 after the
cold boot. The intermediate `M98WRP5.DLL` used the same source but a different
link configuration and is not counted as evidence for the final binary.

The Notepad++ 8.9.8 loader then advanced to the next missing symbol,
`KERNEL32.FindFirstStreamW` (`vm/npp-after-finalpath.png`), and reported
`LAUNCH_FAIL win32_error=31`. This is **not** Notepad++ startup or general
handle-path compatibility. Other file handles and FAT32 variants remain
unsupported or untested.
