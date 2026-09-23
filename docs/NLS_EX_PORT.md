# Locale-name string mapping and comparison on Windows 98

This work adds two `KERNEL32.DLL` KernelEx entries:
`LCMapStringEx` and `CompareStringEx`. The implementation is isolated in
[`src/m98nls_ex.c`](../src/m98nls_ex.c). The host test DLL uses a
small fixture resolver; production integration must bind the synchronized
`m98_locale_name_to_lcid` already in `src/m98wrap.c`.

## Contract and Win98 boundary

The original Korean Windows 98 SE OEM `KERNEL32.DLL` export manifest records
both `LCMapStringW` and `CompareStringW`. The Ex bridge resolves an installed
locale name to an LCID using the existing Win98 resolver, then looks up the
actual Unicode W implementations in KernelEx `KEXBASES.DLL`'s
`get_api_table`. In the production `KernelEx` directory, the bridge loads
that sibling DLL by absolute path on the first Ex call. An independent test
fixture outside that directory may use an already loaded provider or native
W implementation on a modern host. It handles `NULL` as the user default, the
system-default name, and installed language/region names. The empty
invariant name is deliberately unsupported: substituting `en-US` would give
incorrect collation. Unsupported script or custom locales fail explicitly.

The accepted comparison flags are the legacy `NORM_IGNORECASE`,
`NORM_IGNORENONSPACE`, `NORM_IGNORESYMBOLS`, `SORT_STRINGSORT`, and
`LOCALE_USE_CP_ACP`; the last flag is stripped for the Unicode W call.
The accepted mapping operations are `LCMAP_LOWERCASE`, `LCMAP_UPPERCASE`,
and `LCMAP_SORTKEY`, with `NORM_IGNORECASE` and `NORM_IGNORESYMBOLS` for sort
keys. The active KernelEx mapping path ignores byte reversal, Japanese
width/kana, Chinese form, and some other sort-key modifiers; those requests
fail explicitly instead of returning an unchanged string. Comparison width
and kana modifiers likewise fail because the active collation path does not
apply them. Invalid combinations and unknown flags fail. Modern linguistic
casing, linguistic ignore flags, numeric collation, titlecase, hash, and sort
handles fail with `ERROR_NOT_SUPPORTED`; the bridge does not return a
success-shaped approximation. A nonnull NLS version structure also fails
because Win98 cannot promise that modern NLS version. Reserved pointer and
handle arguments fail with `ERROR_INVALID_PARAMETER`.

`LCMapStringEx` returns UTF-16 character counts for mapped strings and **byte
counts** for sort keys. A negative source length delegates the null-terminated
form; explicit positive lengths preserve the caller's boundary. Size queries
and short buffers are covered by the direct test. This is a bounded legacy
NLS profile. Modern Windows' Unicode database and locale-sensitive sort
weights may differ, so neither API is counted as whole-surface compatible
until the integrated guest and wider locale corpus pass.

## Source and license review

- [Microsoft `LCMapStringEx`](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringex) specifies mapping versus opaque byte sort keys, source and destination unit counts, size queries, and reserved parameters.
- [Microsoft `CompareStringEx`](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringex) specifies the three `CSTR_*` results, explicit string lengths, sort flags, and locale-name behavior. Its documentation also warns that sort behavior changes between Windows releases.
- Pinned [Wine `kernelbase/locale.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/locale.c) was read at `CompareStringEx` and `LCMapStringEx`. It resolves named Unicode sort records and uses Wine's NLS tables; those records are not Win98's installed NLS data.
- Pinned [ReactOS `winnls/string/locale.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/winnls/string/locale.c) was read at both Ex entry points. Its locale-name handling and Wine-derived sort tables depend on NT-era infrastructure unavailable to a direct Win98 wrapper.
- Pinned [KernelEx `Kernel32/locale.c`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/Kernel32/locale.c) and its [sort-key implementation](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/Kernel32/locale_sortkey.c) were checked against the installed guest's active `KERNEL32` behavior. KernelEx may redirect the native-named W call; an OEM export entry alone does not prove which implementation executed.
- Pinned KernelEx [`kexbases/main.c`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/main.c), [`Kernel32/_kernel32_apilist.c`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/apilibs/kexbases/Kernel32/_kernel32_apilist.c), and [`common/kexcoresdk.h`](https://github.com/metaxor/KernelEx/blob/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1/common/kexcoresdk.h) define the provider's null-terminated tables, sorted named APIs, and the two real W implementation pointers. The bridge bounds/checks the table; a present but invalid provider returns `ERROR_BAD_FORMAT`, never a native fallback. KernelEx `DllMain` also runs process initialization when the module is loaded; the new lazy-load path therefore needs a real guest test, not only a host mock.
- Microsoft [`GetModuleFileNameA`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamea) documents retrieval of the current module's full path, and [`LoadLibraryA`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibrarya) accepts an absolute path. `m98_nls_ex_set_module` stores the wrapper's `hinstDLL` in `DllMain`; the first Ex call builds the sibling `KEXBASES.DLL` path and loads it outside the loader lock. The returned module path is checked against that exact path, case insensitively for ASCII. A failed absolute load preserves its Win32 error; a different or malformed provider returns `ERROR_BAD_FORMAT`.

The new bridge and fixture code are written for this project under
GPL-2.0-only. No Wine, ReactOS, or KernelEx implementation code or tables
were copied into these files. The project must keep its existing LGPL notices
for separately included KernelEx/Wine-derived files.

## Verification and observed guest behavior

Run `./tools/build-nls-ex.ps1`. It builds an isolated PE32/i386 KernelEx
fixture DLL, a direct table smoke test, an original-name W API probe, and a
static Ex-import probe. The checker reads the pinned OEM export manifest and
requires Windows 98 subsystem 4.10, native KERNEL32 imports, and the intended
two static Ex imports. The host direct smoke verifies sorted API-table ABI,
mapping/string size, sort-key byte size and short buffers, comparison return
classes and lengths, name resolution, and explicit unsupported failures.
This host execution is not a Windows 98 guest result.

The directly installed Windows 98 guest ran the initial
`tests/nls_ex_native_probe.c` build (SHA-256
`e3ecb340c72f54367e80bdb8b4e0e8ec70725b2b8d5fb2fb2cfc63a27dde890b`).
English comparison/casing and Korean Unicode mapping reached the sort-key
check, which failed. A diagnostic rebuild (SHA-256
`52de7f65c7e7ec882bfaa9535283da392f570092380786426366046f8c563f08`)
measured `sort_key_query=21`, `sort_key_written=20`, and a terminal `00`
byte for `en-US` and `L"abc"`. `sort_key_last_error=2` was read after a
successful payload call and is not a failure indication. The revised native
probe (SHA-256
`a6df030aa9e341e33ef85fedfe9322d27b1cfcaa67f05f82988701770c166afc`)
then exited 0 in the same Win98 guest and reported the one-byte discrepancy
as an observation, followed by `PASS: active Win98 KERNEL32 Unicode NLS
baseline`. The current bridge
answers a sort-key size query by generating the key in a private buffer and
returning the actual byte count, which also works on a host whose W API
returns equal query and payload lengths. An earlier fixture DLL (SHA-256
`6b59ab368846209a9907f4383ddffaa2c5cc081defac25f0054a3006629566a3`)
and direct smoke EXE (SHA-256
`eb1aaedad9eae38e787f8e2864a695e2fafd0085b763256e77fae4a3c2063144`)
did pass in the guest with exit code 0. The final flag audit then rejected
additional silent no-op paths, so that earlier run does not validate the
latest binary. The final bounded fixture DLL (SHA-256
`1d416df7fe8fec8f0eb108a5aa8fc8b6ea4af2ef04567d963ab4f8c9f95aa8ae`)
and its direct smoke EXE (SHA-256
`b97265182cd38421023ee1815a3689c41b16477e8545d416d6c2d1f4c24d4c4f`)
then passed on the directly installed Win98 SE guest with exit code 0 and
`PASS: NLS Ex bridge host ABI and bounded behavior`. This used the isolated
fixture resolver, so it does not yet prove integration into the production
`m98wrap.dll` or a static Ex import in the guest.

After the first production integration, the guest's 54-entry wrapper passed
a direct-table smoke, but static `CompareStringEx` returned `0` and
`GetLastError=120` (`ERROR_CALL_NOT_IMPLEMENTED`). In that same process an
application's original-name `CompareStringW` import returned `CSTR_EQUAL`,
while a direct call through the app-local wrapper's API table also returned
`CSTR_EQUAL`. This is evidence that the KernelEx API library's own
`KERNEL32.CompareStringW` import can bind to the original Win98 stub even
when an application's import receives the KernelEx override. The new source
therefore selects `CompareStringW_new` and `LCMapStringW_new` from the loaded
`KEXBASES.DLL` API table. It does not cache a pointer to an unowned module,
load a DLL through the app search path, or treat the native stub's failure as
success.

The provider lookup was checked on the host with a temporary mock
`KEXBASES.DLL` in ignored `build/` output: calls selected the mock API-table
functions instead of the native W imports. The resulting final isolated
fixture DLL (SHA-256
`2bb0b9f07480a3933533861e72fef9331d2c9fa551af1356e9d87f1539b7f4a9`)
and direct smoke EXE (SHA-256
`3a61e0ad1c4de69bcfab3ffd1bad6efc573bef568e09de592c5014f93c1137bd`)
also passed on the directly installed Windows 98 guest with exit code 0 and
`PASS: NLS Ex bridge host ABI and bounded behavior`.

The first 59-entry production wrapper build then exposed a load-order case.
After installation and a normal guest reboot, `NLSI13.EXE` reported a static
`CompareStringEx` result of `0` with `GetLastError=120` even though the IAT
pointed into the new provider (`0x10003499`) and an app-local direct-table
call returned `CSTR_EQUAL`. Follow-up `NLSI14.EXE` printed
`KEXBASES before=00000000` and `KEXBASES after=00000000` around that failed
static call. A separate `NLSFORCE.EXE` loaded
`C:\\WINDOWS\\KernelEx\\KEXBASES.DLL` before the Ex calls, then exited 0
with both mapping and comparison checks passing. Its post-success
`GetLastError=7E` is sticky and not a call failure. These two guest probes
isolate load timing as the cause of the original `120` error. The bridge now
stores its own module handle on attach and, when
installed in a directory named `KernelEx`, loads only its sibling
`KEXBASES.DLL` by absolute path on the first Ex call. It does not attempt a
bare-name load or silently fall back to the original Win98 W stub in that
production layout. The module is pinned for the process lifetime so the
function pointers remain valid across subsequent calls. A process that
unloads and reloads the wrapper repeatedly may retain extra KEXBASES
reference counts until process exit; this is preferable to dangling code
pointers and should be revisited if repeated app-local reload becomes a real
workload. Host PE gate and direct smoke pass. A cold mock-provider lazy-load
host test exited 0 when the provider was initially absent, and a separate
missing-provider test exited 0 only after observing `ERROR_MOD_NOT_FOUND`
instead of a native W success. The revised fixture DLL SHA-256 is
`a760b0a4d1be6185dd0fd9990659cf002dd80a395dff1de4af01465d32c5b7a7`.
The revised production wrapper was installed as
`C:\\WINDOWS\\KernelEx\\M98WRP14.DLL`, SHA-256
`237e42d91c023ac215d059f5dc7deda023118b2299289e5851d14c1d9efb8417`.
The byte-preserved installed `CORE.INI` SHA-256 was
`2da54a399deb15b9960ffd3aaa1046c87e34d599c13e3a33da3a68edd87e6618`.
After a normal shutdown and cold boot under the confirmed WHPX/NEM hardware
backend, the ordinary `NLSI14.EXE` static-import probe (SHA-256
`b756589b4705efa15ba47004f61804452dd5bb3bc1a6a006cb07a61d825d6a46`)
exited **0** with no failure output. That executable does not perform the
diagnostic forced KEXBASES load. Its `CompareStringEx` equality and
`LCMapStringEx` uppercase assertions therefore passed through the installed
provider's runtime loading path. This focused result does not establish full
modern collation equivalence or Notepad++ functionality.

## Integration steps for the owner of `m98wrap.c`

1. Include `m98nls_ex.h` in `src/m98wrap.c` and add `src/m98nls_ex.c` to every
   `m98wrap.dll` source list in `build.ps1`, focused build scripts, and release
   build scripts.
2. After `InitializeCriticalSection(&m98_locale_lock)` in `DllMain`, call
   `m98_nls_ex_set_module(instance)` and
   `m98_nls_ex_set_resolver(m98_locale_name_to_lcid)`. These setters do not
   load any DLL. Before deleting that lock on detach, clear the resolver with
   `m98_nls_ex_set_resolver(0)` and module with `m98_nls_ex_set_module(0)`.
3. Insert `M98_API("CompareStringEx", m98_CompareStringEx)` before
   `CompareStringOrdinal` and `M98_API("LCMapStringEx", m98_LCMapStringEx)`
   after the `Initialize*` entries in the sorted KERNEL32 table.
4. Extend the native-import checker with the verified OEM exports
   `LCMapStringW`, `CompareStringW`, `IsBadReadPtr`,
   `IsBadStringPtrA`, `GetModuleFileNameA`, `LoadLibraryA`,
   `FreeLibrary`, and `InterlockedCompareExchange`, plus the actual linked
   heap calls. Then extend the
   integrated smoke count and build the real wrapper.
5. Run the revised native probe and isolated fixture smoke in the Win98 guest;
   then install a versioned wrapper, cold boot under the confirmed VT backend,
   run the static `nls_ex_import_probe.exe`, and retry the pinned Notepad++
   application. Record each gate separately.

The subsequent combined-provider regression also passed after an accelerated
cold boot with `M98WRP16.DLL`, SHA-256
`e519192a39ca04f5bc2563a9fd42ac3d4d406209b4bd024ded481a3c3d9714b9`.
The ordinary static `nls_ex_import_probe.exe` (SHA-256
`b756589b4705efa15ba47004f61804452dd5bb3bc1a6a006cb07a61d825d6a46`)
returned exit 0 with no output. It was built without `M98_FORCE_KEXBASES`.
The machine-readable receipt, test scope and limitations are linked in
`benchmarks/api-guest-evidence-v1.json`.
