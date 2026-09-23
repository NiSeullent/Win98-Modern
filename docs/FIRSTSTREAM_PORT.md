# FindFirstStreamW Windows 98 FAT32 port

## Observed blocker and behavior

After the `GetFinalPathNameByHandleW` loader import was supplied, the
installed Windows 98 SE guest displayed `KERNEL32.DLL!FindFirstStreamW` as
the next missing Notepad++ 8.9.8 import (`vm/npp-after-finalpath.png`). This
port adds its four-argument ABI to the sorted KernelEx KERNEL32 API table.
Supplying the loader name is not evidence that Notepad++ starts.

[Microsoft's `FindFirstStreamW` contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirststreamw)
says a filesystem without stream support must return `INVALID_HANDLE_VALUE`
and `ERROR_INVALID_PARAMETER` (87). Microsoft's [filesystem comparison](https://learn.microsoft.com/en-us/windows/win32/fileio/filesystem-functionality-comparison)
lists FAT32 named and alternate data streams as unsupported. Consequently,
this wrapper **does not synthesize** a successful `::$DATA` stream result on
Windows 98 FAT32. That spelling is the default data stream returned on a
filesystem that actually supports stream enumeration.

The implemented subset requires a fully qualified local drive path that
round-trips through the installed ANSI code page. It checks that the file or
directory exists, obtains the local volume's filesystem name from native
`GetVolumeInformationA`, and returns error 87 only when the name is `FAT` or
`FAT32` and the volume does not advertise named streams. A missing path keeps
the native file/path error. Invalid information level, reserved flags, null
path, or null output buffer return error 87 before any filesystem access.
UNC, remote-drive, and other filesystem paths return
`ERROR_NOT_SUPPORTED`; relative paths return `ERROR_INVALID_NAME`; long or
non-round-tripping paths fail explicitly. The output buffer is untouched on
every failure. No success-shaped search handle is created.

`FindNextStreamW` is not among the pinned Notepad++ executable's ordinary
imports and is not added here. Since this function never returns a search
handle, there is no valid follow-up stream enumeration or `FindClose` call.
Win98 already exports native `FindClose`. A future implementation that can
return a stream search handle must implement its lifetime and `FindClose`
integration; a fake handle would leak or fail on close.

## Source review and licensing

- Pinned [Wine `dlls/kernelbase/file.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/file.c#L1500-L1508) returns `ERROR_HANDLE_EOF` unconditionally for both stream functions. This is not the documented FAT32 failure behavior and was not copied.
- Pinned [ReactOS `dll/win32/kernel32/client/file/find.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/client/file/find.c#L957-L1151) opens the file, then uses NT `FileStreamInformation` to enumerate streams and a private heap search handle. Its [FastFAT query switch](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/drivers/filesystems/fastfat/fileinfo.c#L535-L607) does not support that information class and returns an invalid-parameter status. The NT query and handle representation are unavailable on Windows 98.
- The [Microsoft `FindNextStreamW`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextstreamw), [`FindClose`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findclose), and [`WIN32_FIND_STREAM_DATA`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-win32_find_stream_data) contracts were checked for handle lifetime and the 600-byte output ABI, even though this FAT32 subset returns no output.

No Wine or ReactOS code was copied. This is original project code under
GPL-2.0-only. The new native imports `GetDriveTypeA`,
`GetFileAttributesA`, and `GetVolumeInformationA` are present in the pinned
Korean Windows 98 SE OEM KERNEL32 export manifest; the static PE gate checks
the actual linked binary against that manifest. `WideCharToMultiByte` and
`MultiByteToWideChar` were already original-Win98 imports.

## Verification and guest handoff

Run `./tools/build-stream.ps1` from the repository root. It builds
`build/stream/m98wrap.dll`, a direct API-table test, and an EXE with a static
`KERNEL32.FindFirstStreamW` import. The PE gate checks PE32/i386 subsystem
4.10, actual DLL imports against the original Win98 OEM manifest, and the
guest test imports. The direct host test validates the installed filesystem
query, invalid parameters, relative and UNC paths, nonexistent file error,
long path rejection, and untouched output. The static probe runs against the
host's native KERNEL32 and checks its import ABI and stream handle close.
The 42-entry API-table smoke also runs.

The direct and static probes use the installed `KERNEL32.DLL` image as their
existing-file fixture, so they still exercise the system volume if the
probes are launched from an optical test disc. The host system is on NTFS, so
its direct wrapper test expects `ERROR_NOT_SUPPORTED`.

The four host checks passed. The reproducible `build/stream/m98wrap.dll`
SHA-256 is
`4eaece36a67987c7cca21e3f17f9e082f7a5413a59f02afce1dec13912924204`;
two builds produced the same digest. On 2026-09-24 that exact DLL was copied
with a verified SHA-256 into the directly installed Windows 98 SE guest as
`M98WRP7.DLL`, selected in `CORE.INI`, and cold-booted to the GUI under the
current VirtualBox WHPX/NEM backend. The direct test printed
`FILESYSTEM=FAT32` and passed the no-stream error subset with exit 0. The
static `KERNEL32.FindFirstStreamW` import test and the 42-entry sampled API
table smoke passed with exit 0. Notepad++ then reported the next missing
loader symbol, `KERNEL32.GetLocaleInfoEx` (`vm/npp-after-firststream.png`) and
`LAUNCH_FAIL win32_error=31`. This is **not** app startup or complete stream
API compatibility.
