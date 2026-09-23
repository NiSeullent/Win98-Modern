# Independent USER32 clipboard contract tests

The selected clipboard batch retains **all 23 USER32 names** in the project
catalogue: `AddClipboardFormatListener`, `RemoveClipboardFormatListener`,
`GetUpdatedClipboardFormats`, `ChangeClipboardChain`, `CloseClipboard`,
`CountClipboardFormats`, `EmptyClipboard`, `EnumClipboardFormats`,
`GetClipboardData`, `GetClipboardFormatNameA`, `GetClipboardFormatNameW`,
`GetClipboardOwner`, `GetClipboardSequenceNumber`, `GetClipboardViewer`,
`GetOpenClipboardWindow`, `GetPriorityClipboardFormat`,
`IsClipboardFormatAvailable`, `OpenClipboard`, `RegisterClipboardFormatA`,
`RegisterClipboardFormatW`, `SetClipboardData`, `SetClipboardViewer`, and
supplemental Wine `GetClipboardMetadata`. The three new listener/format
functions depend on original Win98 window messages, viewer-chain operations,
clipboard locking/enumeration, and data operations. This suite certifies only
the behavior it actually exercises; catalogue presence is not a pass.

## Sources and expected behavior

- Microsoft's [AddClipboardFormatListener](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-addclipboardformatlistener),
  [RemoveClipboardFormatListener](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-removeclipboardformatlistener),
  [GetUpdatedClipboardFormats](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getupdatedclipboardformats),
  and [WM_CLIPBOARDUPDATE](https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-clipboardupdate)
  define the three `BOOL WINAPI` signatures and posted update message with
  zero parameters.
- Pinned Wine `df15af3652511150490934682202d45af892f887`
  [`dlls/win32u/clipboard.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/win32u/clipboard.c)
  lines 303-325/501-528 and
  [`server/clipboard.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/server/clipboard.c)
  lines 216-264/305-340/469-495 supplied independent behavior references:
  duplicate/missing listener rejection, cleanup on destroyed windows, posted
  notification and complete current-format snapshots. Its
  [`dlls/user32/tests/clipboard.c`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/user32/tests/clipboard.c)
  lines 1175-1189/1368-1379/2103-2205 provided oracle test cases.
- Pinned ReactOS `9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8`
  [`win32ss/user/user32/windows/clipboard.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/user/user32/windows/clipboard.c)
  lines 382-409 contains **unimplemented** bodies for the three modern
  exports; these are not counted as working evidence. Its native clipboard
  ownership, viewer-chain and enumeration bodies in
  [`win32ss/user/ntuser/clipboard.c`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/win32ss/user/ntuser/clipboard.c)
  informed dependency analysis only. No Wine/ReactOS implementation body is
  copied into the independent tests.

## Test paths and clipboard safety

`tests/clipboard_smoke.c` creates native windows and pumps their messages.
Its default path is **read-only**: original modern host USER32 (`--require-api`),
direct `CLIPFIX.DLL` API table (`--bridge`), or direct shipping
`M98USER.DLL` API table (`--shipping`). `tests/clipboard_import_probe.c`
provides a short static-import route check. Building the same full smoke
translation unit with `M98_STATIC_CLIPBOARD_IMPORTS=1` produces
`clipboard_static_suite.exe`; its actual PE import table requires all three
ordinary `USER32.DLL` symbols before process entry. On a routed Win98 guest
it runs `--bounded-provider --guest-mutate` so the foreign-HWND expectation
matches this deliberately own-process backend.

The shared test cases check duplicate registration, missing removal, invalid
and destroyed HWNDs, a same-process window owned by another thread and its
thread-exit cleanup, sufficient/short/NULL format buffers, no partial write,
and a query while another thread in the process has the clipboard open.
Original modern Windows accepts registration of the desktop HWND. The bounded
provider explicitly rejects foreign-process HWNDs with error 50; the test
records this difference instead of manufacturing native equivalence.

`--guest-mutate` is explicitly guarded to the actual Windows 98 4.10 guest.
It checks that the starting clipboard has **zero formats** and skips all
data-changing work otherwise. Only an empty original state can be restored
exactly without risking user data. The test writes owned `CF_TEXT` and
`CF_UNICODETEXT`, reads both back with native `GetClipboardData` and checks
the `M98` payload, verifies format identifiers and post-close synthesized
formats, and checks both `WM_CLIPBOARDUPDATE` and legacy
`WM_DRAWCLIPBOARD`. It then makes six owned `Open`/`Empty`/`Set`/`Close`
changes without pumping the caller's queue between them and requires at
least six notifications from each path. Worker scheduling remains concurrent,
so this is a burst regression rather than a proof that the worker was paused.
The final empty-state restore is required for the distinctive
`PASS: owned guest clipboard mutation, notifications, formats, and empty restore`
marker. A skipped mutation cannot emit that marker, and the guest suite
requires it. Host builds never execute this mutation mode.

`tests/clipboard_check_pe98.py` checks i386 PE32, subsystem 4.10, no TLS,
delay-import or CLR directories, and original OEM native import allowlists.
It checks both fixture and shipping DLLs, the dynamic smoke, the small
static probe, and the **full** static suite. The last two must each have the
three new ordinary USER32 imports; the dynamic smoke and provider must not
depend on those absent original Win98 names. The provider DLLs must expose
`get_api_table`. `tools/build-clipboard-tests.ps1` performs these gates plus
read-only native host, static host, fixture and shipping tests. It does not
run the guest mutation option.

## Independent evidence and negative control

The host read-only build/contract gate passed for all five image paths. The
full static suite was built as SHA-256
`b1f2d883d02910dfbd781efdd4fbc00364a52506521a782740bbad5e37adcbf2`;
the dynamic full smoke was
`fd1ea2bc8e5121ca5d3f93d0600776bcd71cfcef21be41c2979457aad7bff08b`.
The host static result proves that the test executable really binds through
USER32, not that KernelEx routing in Win98 is already working.

The actual Windows 98 guest first exposed a concrete backend defect. The
pre-fix direct mutation receipt
`build/guest/receipt-clipboard-pre4-diagnostic.json` has exit 1: original
`CountClipboardFormats` reported four entries, sequence stayed fixed, and
manual `OpenClipboard`/`EnumClipboardFormats` enumerated IDs `1,13,16,7`,
yet the bridge's closed-clipboard query returned success with count zero.
The original closed `EnumClipboardFormats(0)` had returned zero with last
error zero. The backend was corrected to open temporarily when native count
is nonzero despite that ambiguous result. This negative control is retained
and no assertion was weakened.

The corrected direct-table fixture and shipping probes then passed on the
same guest, recorded by `build/guest/receipt-clipboard-pre5-fixed.json` and
`receipt-clipboard-pre5-shipping.json`. Those earlier probes did not yet test
payload round-trips or burst messages. The stronger guest run used the
dynamic smoke hash above and the later worker implementation: receipts
`build/guest/receipt-clipboard-burst1-fixture.json` and
`receipt-clipboard-burst1-shipping.json` both record exit 0, no timeout,
the distinct mutation/restore pass marker, stable four-format result matching
native IDs `1,13,16,7`, and six update messages plus six legacy draw messages
for six queued changes. The fixture output SHA-256 is
`9ea8f33569de8be92a3ce8461130b034327118866db1d7245c472349b9fe4cd2`;
shipping output SHA-256 is
`bfa47ad77d78bb388be9c06acee8e348bf8d60d7d7bf1967023a71a277f67b84`.
These two calls use `get_api_table` directly, not installed KernelEx routing.

The same Win98 guest previously passed read-only direct-table fixture and
shipping probes in `build/guest/receipt-clipboard-pre2-direct.json` and
`receipt-clipboard-pre2-shipping.json`. The first attempt assumed that another
thread in the same process opening the clipboard must deny enumeration;
modern native USER32 and the Win98 guest both allowed it. The test was
corrected to reflect that observed behavior. Direct-call success does not
establish support for foreign HWNDs, all 23 clipboard contracts, OLE, delayed
rendering, arbitrary third-party viewer chains, or target application startup.

## Review and promotion boundary

`src/m98user.c` and the fixture compile the same sorted USER32 table and
backend. The initial Win98 closed-enumeration bug and rapid-change sequence
de-duplication bug were corrected after direct guest failures/review. The
worker's failed-start self-reference is released with
`FreeLibraryAndExitThread` and a failed class registration does not unregister
another class. An independent read-only review found no further concrete
race or use-after-free in that narrowed source version. It does not erase the
documented HWND-destroy/post race, unsupported foreign windows, unresponsive
viewer-chain behavior or abrupt process-kill cleanup gap.

The reviewed shipping image SHA-256
`4932584fcb7f1af493dbebfd76a38ede4f04b5543a0559cdd8fd1c33ff8b3fc7`
was installed as `M98USR1.DLL`; the cold-boot CORE configuration
`build/guest/core-m98usr1.ini` SHA-256
`2e853bcdd3a686ad8d105ee47219049efcdd9fbcfeddeac29c9d061eadd0bcc9`
registers `m98usr1` and routes exactly the three `USER32` names to table 0
in each of its three active profiles. The resulting independent
`remote/suites/api-clipboard.json` execution is recorded in
`build/guest/suite-clipboard-user1.json`: **6/6 passed, exit 0, no timeouts**
on the directly installed Windows 98 SE guest. This includes the tiny static
import probe and the full static-import contract/mutation suite, plus direct
fixture and shipping read-only/mutation paths. The full static output SHA-256
is `9b37e240f43eeb730f7e9356118955b62b87a7b7089838e4e09d18219165f7de`;
it includes the static binding observation, stable synthesized IDs
`1,13,16,7`, six/six burst notifications, content checks through the
pass condition, and the empty-restore marker. This is evidence for the three
routed exports and tested native dependencies in this guest, not for all
23 clipboard names or a selected application starting successfully.
