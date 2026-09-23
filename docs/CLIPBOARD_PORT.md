# USER32 clipboard listener and format-query port

Status: `M98USER.DLL` KernelEx API library and `CLIPFIX.DLL` direct-table
fixture; the same provider bytes installed as `M98USR1.DLL` passed the real
Win98 USER32 route's six-test suite after a fresh accelerated boot. The
current work implements three missing exports using
Win98's existing clipboard, window and message services. It does not replace
USER32, reimplement all native clipboard behavior, or establish application
startup or full clipboard-family compatibility.

## Complete selected inventory

The pinned SDK/Wine/ReactOS/KernelEx/One-Core catalogue now assigns the
clipboard names to a dedicated `clipboard` batch, separated from the broader
`ui-theme` queue. Its 23 USER32 clipboard names are retained below. They were selected from
the whole work queue, rather than adding only the latest failed app import.
The named catalogue rows are discovery and dependency evidence, not behavior
certification. Native presence below means presence in the extracted original
Windows 98 SE OEM export manifest.

| Public or supplemental entry | Current backend and evidence boundary |
| --- | --- |
| `AddClipboardFormatListener` | New worker/viewer-chain registration, own-process HWND subset. |
| `RemoveClipboardFormatListener` | New registration removal using the same worker state. |
| `GetUpdatedClipboardFormats` | New complete current-format snapshot using native enumeration. |
| `ChangeClipboardChain` | Native viewer-chain dependency. |
| `CloseClipboard` | Native clipboard lock/notification dependency. |
| `CountClipboardFormats` | Native, retained without replacement. |
| `EmptyClipboard` | Native, retained without replacement. |
| `EnumClipboardFormats` | Native snapshot dependency. |
| `GetClipboardData` | Native, retained without replacement; the bridge never reads data. |
| `GetClipboardFormatNameA` | Native, retained without replacement. |
| `GetClipboardFormatNameW` | Native name plus KernelEx Unicode source declaration; wider Unicode behavior is not certified here. |
| `GetClipboardOwner` | Native, retained without replacement. |
| `GetClipboardSequenceNumber` | Native, retained without replacement; tests observe sequence changes. |
| `GetClipboardViewer` | Native viewer-chain dependency. |
| `GetOpenClipboardWindow` | Native, retained without replacement. |
| `GetPriorityClipboardFormat` | Native, retained without replacement. |
| `IsClipboardFormatAvailable` | Native, retained without replacement. |
| `OpenClipboard` | Native snapshot dependency when the caller has not opened it. |
| `RegisterClipboardFormatA` | Native, retained without replacement. |
| `RegisterClipboardFormatW` | Native name plus KernelEx Unicode source declaration; not reimplemented. |
| `SetClipboardData` | Native, retained without replacement. |
| `SetClipboardViewer` | Native viewer-chain dependency. |
| `GetClipboardMetadata` | Supplemental Wine declaration, absent from the pinned SDK candidate set; no implementation claim. |

There are 22 SDK candidates and one supplemental Wine candidate in this list.
Nineteen names occur in the native export manifest; that is not nineteen
validated modern contracts. The three new exports are absent from that
manifest. In the actual selected app files, Notepad++ imports Add/Remove
normally, while Chromium 150 and Supermium import them with delay loading.
This makes the shared clipboard/message backend relevant to three selected
apps without making any startup claim.

Related message dependencies are `PostMessageA`, the worker's
`GetMessageA`/`TranslateMessage`/`DispatchMessageA`, `SendNotifyMessageA`,
`SendMessageTimeoutA`, window creation/destruction, and properties. The broader
USER32 catalogue also retains message filters, broadcast, DPI and input-message
families; they are not silently included in the behavior supplied by this DLL.

## Exact sources reviewed

Source archives are pinned by `porting/sources.json` and the local
`build/api-sources/receipt.json`. Implementation bodies, rather than only export
declarations, were read from both projects:

- Wine `df15af3652511150490934682202d45af892f887`,
  [`dlls/win32u/clipboard.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/win32u/clipboard.c):
  `NtUserGetUpdatedClipboardFormats` at 303–325 and listener wrappers at
  501–528 marshal to the Wine server. NULL output count yields
  `ERROR_NOACCESS`; the format list is the current list, not a change history.
- Wine
  [`server/clipboard.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/server/clipboard.c):
  listener add/remove at 216–255, posted update notifications at 258–264,
  close-time notification and synthesized formats at 268–274, window/thread
  cleanup at 306–340, and current format enumeration at 470–495. The server
  rejects duplicate registration and missing removal with invalid parameter;
  it removes listeners when their window object is destroyed. A short format
  buffer does not receive a partial list.
- Wine
  [`dlls/user32/tests/clipboard.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/user32/tests/clipboard.c):
  registration/error checks at 1175–1189 and 1368–1379; current-format,
  NULL/short buffer, count and synthesized-format checks at 2098–2205.
- ReactOS `9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8`,
  [`win32ss/user/user32/windows/clipboard.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/user/user32/windows/clipboard.c):
  all three modern exports at 382–409 are `UNIMPLEMENTED`/FALSE. These bodies
  are explicitly not used as a successful implementation or behavior oracle.
- ReactOS
  [`win32ss/user/ntuser/clipboard.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/user/ntuser/clipboard.c):
  window cleanup at 414–440, ownership-checked enumeration at 443–485,
  close-time synthesis/notification at 545–585, chain removal at 624–655,
  viewer insertion at 1114–1155 and sequence number at 1164–1184. NT
  window-station/object lifetime and locking cannot be transplanted by name
  into Win98.
- KernelEx `31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`,
  `apilibs/kexbasen/user32/uniuser32.c`: ANSI-backed
  `GetClipboardFormatNameW_new` and `RegisterClipboardFormatW_new` bodies.
  `apilibs/kexbases/Kernel32/thread.c::CreateThread_fix` supplies a writable
  thread-ID pointer. This provider's native `CreateThread` bypass uses its
  own real DWORD output pointer. The mirrored `src/kex_abi.h` defines the
  KernelEx API-library table ABI.

The three new source files are original project code under GPL-2.0-only.
Wine's LGPL implementation bodies and ReactOS's kernel/object structures are
not copied. The public [Add](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-addclipboardformatlistener),
[Remove](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-removeclipboardformatlistener),
[GetUpdated](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getupdatedclipboardformats)
and [WM_CLIPBOARDUPDATE](https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-clipboardupdate)
contracts define the public signatures and posted message with zero parameters.

## Shared backend and ownership

`src/m98user.c` has a sorted three-name USER32 table. Every entry is `BOOL
WINAPI`; the arguments are respectively `HWND`, `HWND`, and
`PUINT formats, UINT capacity, PUINT count_out`. `get_api_table` is the
KernelEx cdecl entry, with a terminating empty table. `CLIPFIX.DLL` compiles
the same table and implementation, without a test-only behavioral substitute.

The first registration initializes one hidden native window on a dedicated
worker thread. Registration nodes, generation cookies and viewer-chain state
are owned by that worker. Commands carry only integer operations and HWNDs;
they do not share caller-stack pointers with the worker. The API uses
`SendMessageTimeoutA` with `SMTO_BLOCK`, so it does not dispatch the
application's pending window messages while waiting. The worker pumps only
its own queue.

The worker joins the native viewer chain before publishing the first
registration. Its initial `WM_DRAWCLIPBOARD` is not treated as a data change.
Each later `WM_DRAWCLIPBOARD` results in real
`PostMessageA(WM_CLIPBOARDUPDATE,0,0)` calls. Sequence-based de-duplication was
removed: two queued notifications for separate changes can both read the
newest sequence number when the worker finally receives them, which would
incorrectly discard the second change. Only the one initial draw generated
by `SetClipboardViewer` is suppressed. Legacy `WM_DRAWCLIPBOARD` and
`WM_CHANGECBCHAIN` messages continue
through the next viewer using `SendNotifyMessageA`; an application thread
blocked in Add/Remove is not synchronously called to forward them.

Each listener window has worker-owner and generation-cookie properties.
Native window destruction removes its properties; periodic maintenance and
each command/notification discard registrations whose window, thread or
cookie no longer matches. Re-registering a recycled HWND cannot inherit the
old node merely because its numeric handle matches. Removal clears the
registration before returning; a message already posted before removal is
not removed from the caller's queue.

When the list becomes empty, a posted worker sweep unlinks the viewer. This
runs after the Remove command has returned because the legacy chain-removal
API can synchronously call the next viewer. Reentrant Add during this
unlink returns `ERROR_BUSY`, preventing reinsertion from corrupting the
saved next pointer. Another process inserting a new head immediately after
our insertion is not incorrectly classified as insertion failure.

DllMain only records the DLL instance. Initialization obtains one ordinary
`LoadLibraryA` self-reference outside loader lock so worker code and its
WndProc remain mapped. The hidden window, worker, ready event, registered
class and pin are bounded process-lifetime backend resources. There is no
public shutdown API. Removing the last listener does not unload this backend;
it unlinks the clipboard viewer. No application callback, blocking cleanup or
thread wait runs in DllMain.

If worker startup fails after obtaining the self-reference, it cleans up any
window/class it actually created, publishes the error, wakes initialization
waiters and calls native `FreeLibraryAndExitThread` to release that extra
reference without returning into potentially unmapped worker code. A failed
class registration does not unregister someone else's existing class. This
failure path was independently reviewed; fault-injected resource exhaustion
has not been executed.

## Format snapshot behavior

`GetUpdatedClipboardFormats` returns the native current format list, including
formats that Win98 synthesizes. It never reads clipboard payloads, forces
delayed data rendering, writes clipboard contents, or invents modern formats.

It first tries enumeration so a clipboard already opened by the caller is
not spuriously closed. If native enumeration reports
`ERROR_CLIPBOARD_NOT_OPEN` or access denial, it attempts a temporary open.
Original Win98 also returns zero without setting an error when the clipboard
is closed; an apparent empty enumeration with a positive
`CountClipboardFormats` therefore also takes the temporary-open path.
Already-open nonempty enumeration supplies a first format, while an
already-open empty clipboard has count zero; neither is reopened/closed.
The bridge collects the list into dynamically grown private storage and
closes only its temporary open. Caller output is untouched until enumeration
completes. The required
count is reported on short-buffer/NULL-buffer errors, and the format array
is not partly overwritten for insufficient capacity. NULL output count or
an inaccessible required output range yields `ERROR_NOACCESS`.

The code probes writable output memory with the original Win98
`IsBadWritePtr`. Callers must keep their output storage valid for the duration
of the call; concurrent unmapping is not synchronized by this backend.
Allocation growth is checked for integer overflow; no arbitrary format-count
cap is added. Successful calls preserve the incoming last error, as observed
in the host oracle.

## Explicit differences and unverified boundaries

- Registrations are restricted to windows owned by the calling process,
  including other threads in that process. Foreign-process windows, including
  the desktop, fail with `ERROR_NOT_SUPPORTED`. Native Windows permits at
  least the desktop case. A process-local worker cannot preserve a foreign
  listener after the registering process dies; a global owner service or
  native USER integration is required for that broader contract.
- Format queries depend on native enumeration/open rules. If those deny
  access because another thread/process owns the clipboard, that error is
  returned instead of a stale cached list. The host allowed enumeration when
  another thread in the same process held the clipboard; this observed
  success is preserved rather than forcing an artificial failure. The direct
  Win98 probe subsequently observed the same own-process cross-thread success.
- Property checks narrow ordinary destroyed/recycled-HWND mistakes but are
  not atomic with `PostMessageA` under the native USER object lock. Concurrent
  destruction and immediate handle reuse between the final check and posting
  remain outside the proven subset. No full native window-lifetime guarantee
  is claimed.
- An uncooperative/hung legacy viewer can block native
  `ChangeClipboardChain`. It runs on the worker after the API command, and
  later command waits have a timeout, but complete recovery of a broken
  third-party viewer chain is not implemented. Abrupt process termination can
  likewise bypass normal worker unlink. Process shutdown, queue exhaustion,
  forged worker messages and adversarial external property changes are not
  certified.
- The worker self-pin deliberately prevents unloading its active WndProc.
  This is a process-lifetime resource policy, not support for forcibly
  unloading an active backend or terminating its worker.
- Native Unicode clipboard conversions, delayed-rendering owner callbacks,
  OLE clipboard ownership, all 19 original clipboard contracts and the Wine
  supplemental metadata export remain separate validation work. API-set
  aliases and a general USER32 message broker are not supplied by this table.

## Build and evidence

`tools/build-clipboard.ps1` deterministically builds
`build/clipboard/M98USER.DLL` and `build/clipboard/CLIPFIX.DLL` with PE32 x86,
subsystem 4.10 and a zero linker timestamp. When present, the independently
owned `tools/build-clipboard-tests.ps1` runs the PE/native-import gate and
native/fixture contract probes. `-SkipHostExecution` builds and checks PE only.
No clipboard mutation mode is enabled by this builder.

The independent host read-only oracle and direct fixture currently pass:
initial add, duplicate registration (87), invalid/destroyed HWND (1400),
unregistered and duplicate removal (87), own-process cross-thread registration
and thread-exit window destruction, NULL count (998), complete format query,
short output buffer (122), NULL format buffer (998), and controlled
other-thread clipboard ownership. Native desktop registration succeeds;
the bridge explicitly reports its foreign-window limitation (50). The
native static-import probe also passes. One host format query initially met
transient access denial; bounded probe retries record such interference and
do not convert a permanent error into success.

The PE checker confirms the three public ordinary USER32 imports in the
static probe and rejects new API imports from the fixture. Native dependencies
are checked against the original Win98 OEM export manifest, with no NT DLL,
CRT, TLS, delay-import or CLR dependency.

The historical direct Win98 evidence below covers only its pinned builds.
Provider routing/static-import tests and fresh tests for the final code are
separate gates. No CORE registration or application compatibility result
follows from a successful standalone DLL build.

### Preliminary direct Win98 observations

The directly installed Win98 SE guest ran both direct-table DLL variants with
the same read-only probe:

- `CLIPFIX.DLL` SHA-256
  `33d755335207b11a05905aba09af5f53a747fc9abdb0841e61a58e438a25efa9`.
- `M98USER.DLL` SHA-256
  `870551fa5d789268ef6ec024194c6242ea3d28191ef527acfc41b38296e531ed`.
- Probe SHA-256
  `34f93c4ceff8d0bfb1e274915e058d4a7904c406cfcc76f6b218572e5d6c8cb6`.

`build/guest/receipt-clipboard-pre2-direct.json` (`--bridge`) and
`receipt-clipboard-pre2-shipping.json` (`--shipping`) both record exit 0,
no timeout, 844 bytes and output SHA-256
`6f8f7158c906483f377dbebe0a4d516ba09627b08d8a36ee160c89608ff5fd23`.
Both tests use the direct API table; the word shipping selects M98USER and
does not mean that KernelEx production routing has been activated.

The first guest probe incorrectly assumed that another thread in the same
process opening the clipboard must make enumeration fail. Both the modern
native host and Win98 backend allowed this query. The test expectation was
corrected to preserve this observed native success; no implementation change
was made to force an artificial access-denied result.

The pre3 guest mutation tests delivered `WM_CLIPBOARDUPDATE` and passed
explicit CF_TEXT/CF_UNICODETEXT identifiers and short-buffer/no-partial-write
checks, but failed the closed-clipboard format-count comparison. Those runs
remain recorded failures.

The pre4 diagnostic established the implementation bug: before and after
query, native count was four and the clipboard sequence was unchanged, but
the wrapper returned TRUE/count zero. Explicitly opening and enumerating the
same clipboard returned identifiers 1, 13, 16 and 7. Receipt
`build/guest/receipt-clipboard-pre4-diagnostic.json` records exit 1 and output
SHA-256 `6d6b1de99ce31d19d83b22476fad9d027898ddbc9b8f56006d5c6649897f6c54`.
This is the negative control for the silent-zero enumeration fallback.

After the fallback correction, the same diagnostic probe SHA-256
`74274b35783b17bcf7a26fae7fb4f9afac495e1be1f74e125c1f0e9023129b5b`
passed on Win98 with both variants:

| DLL / receipt | DLL SHA-256 | Output SHA-256 |
| --- | --- | --- |
| CLIPFIX / `receipt-clipboard-pre5-fixed.json` | `a7e589d994b9b5fb92aff33d91b88d5da9b1cbc81463c40e583af06ddde7d6e8` | `455b83c86617fd230ec02c828b35b05862dbf4953e3af79d4ce943e0a433b26d` |
| M98USER / `receipt-clipboard-pre5-shipping.json` | `4ba35f5007b9cea97bdcef8f0c7eee014f6f73eff2f85b7b8e43761da59dc75e` | `c86c7092ac603e1b314391f8ecdbaec827e028db04e9f840a9a1905af43ba441` |

Both exited 0 without timeout, returned all four native format IDs, and
delivered the posted clipboard update. These receipts predate the later
burst-notification and startup-pin corrections; they do not certify those
newer binaries.

### Frozen source and direct guest verification

The independent reviewer agreed with the silent-zero fallback, identified
the need to retain all later draw notifications, and reviewed the startup
module-pin cleanup. The final probe adds actual CF_TEXT/CF_UNICODETEXT data
round trips, an initial-registration false-update check, and six consecutive
clipboard changes without pumping the caller queue. Its host read-only modes
pass; clipboard mutation is restricted to the isolated Win98 guest and a
verified initially empty clipboard. The probe restores that empty state.

| Artifact | SHA-256 |
| --- | --- |
| `src/m98_clipboard.c` | `e82b52ff6e8f8adbd51a73dc923e6c25f4edcc080e0e030f4ca3a6cc7a6d0489` |
| `src/m98_clipboard.h` | `e2374e5d6df0bdc1c962b4d8ad39f852a3b18caca696a03f2f259fa18065ebcc` |
| `src/m98user.c` | `14373e0c5e53f220a2336724ce8d518d52842c62b769a49a513103a09401f48d` |
| `build/clipboard/CLIPFIX.DLL` | `ba096c14dbc22c9b3e4612c05dde4bc07c38eb0df721f0ce0337a4c62c464f10` |
| `build/clipboard/M98USER.DLL` | `4932584fcb7f1af493dbebfd76a38ede4f04b5543a0559cdd8fd1c33ff8b3fc7` |
| `build/clipboard-tests/clipboard_smoke.exe` | `fd1ea2bc8e5121ca5d3f93d0600776bcd71cfcef21be41c2979457aad7bff08b` |
| `build/clipboard-tests/clipboard_import_probe.exe` | `fa3cc955e5d3071a08f17fb897a817afe28530b00698daa56e0920044c8e0cd0` |
| `build/clipboard-tests/clipboard_static_suite.exe` | `b1f2d883d02910dfbd781efdd4fbc00364a52506521a782740bbad5e37adcbf2` |

The fixture/provider PE gates, host native read-only oracle, native minimal
and full static-import suites, and both direct DLL variants pass for this
build. Independent final source review found no further concrete defect in
worker startup pin cleanup, queue ownership or Remove command ordering;
the documented native-lifetime and viewer-chain limits remain.

The final direct guest runs used exactly the source/DLL/probe hashes above.
Both returned exit 0 without timeout and 1,836 output bytes:

| Receipt under `build/guest/` | Output SHA-256 |
| --- | --- |
| `receipt-clipboard-burst1-fixture.json` | `9ea8f33569de8be92a3ce8461130b034327118866db1d7245c472349b9fe4cd2` |
| `receipt-clipboard-burst1-shipping.json` | `bfa47ad77d78bb388be9c06acee8e348bf8d60d7d7bf1967023a71a277f67b84` |

Each run checked actual owned CF_TEXT/CF_UNICODETEXT content, the four
post-close native formats, and exactly six `WM_CLIPBOARDUPDATE` and six
legacy `WM_DRAWCLIPBOARD` notifications for six changes without caller-queue
pumping. The initially empty test clipboard was restored. This confirms the
new build's burst behavior; it is not a recorded failing burst run of the
earlier sequence-based implementation.

### Installed USER32 route and static-import verification

The root integrator installed the same `M98USER.DLL` bytes under the versioned
name `M98USR1.DLL`, added the separate `USER32.0` route, and cold-booted the
accelerated Windows 98 guest. `build/guest/suite-clipboard-user1.json` records
all six tests passing, every exit code 0 and no timeout. Its transfer records
verify the fixture, provider and three executables against the hashes above.
The suite retains the hypervisor partition evidence and guest OS 4.10 receipt.

| Suite test | Output SHA-256 |
| --- | --- |
| `clipboard-fixture-read` | `6f8f7158c906483f377dbebe0a4d516ba09627b08d8a36ee160c89608ff5fd23` |
| `clipboard-shipping-read` | `6f8f7158c906483f377dbebe0a4d516ba09627b08d8a36ee160c89608ff5fd23` |
| `clipboard-fixture-update` | `8f703a63e2bd1a31b7c71e56ca3a5bafc5b4e45d7932b6c328887f81cee8a42c` |
| `clipboard-shipping-update` | `9ea8f33569de8be92a3ce8461130b034327118866db1d7245c472349b9fe4cd2` |
| `clipboard-static-imports` | `5c1144639976e5a730e005919887b65a20e4645e29fb30ce29ae069e7138885b` |
| `clipboard-static-full-contract` | `9b37e240f43eeb730f7e9356118955b62b87a7b7089838e4e09d18219165f7de` |

The two static executables import the three names normally from USER32; they
do not obtain them from the fixture's API table. The full static suite covers
the same supported registration/errors, actual payload round trip, format
enumeration, six-change burst and empty restoration through the installed
route. Separate direct-table successes alone would not prove that binding.
The independent test design and negative control are documented in
`docs/CLIPBOARD_CONTRACT_TESTS.md`.

The three-export implementation and evidence are frozen at the hashes above.
This is a tested own-process clipboard subset, not a certification of all
23 names, all selected applications or any overall compatibility percentage.
