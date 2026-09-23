# GDI32 drawing adapter: direct Windows 98 guest checkpoint

On 2026-09-24, the installed Win98 SE 4.10 test guest was running in
`Win98Modern-Accel-128` with `NEM: Created partition` in the current VM log.
`M98GDI.DLL` SHA-256
`4e0fef1de6b9807e7db7056a7937cbafeaf8a20d59cb9dd81e0b0f4a8da5f468`
was copied, hash-verified and promoted as the **unregistered**
`C:\WINDOWS\KERNELEX\M98GDI1.DLL`. No `CORE.INI` route or profile changed.
The direct API-table contract program SHA-256
`1c63f257e26f79b4362c1e58fbd9a662f97a7c4f8796d965154872d7bdf45057`
was copied to `C:\M98LAB\GDCON1.EXE` with the remote transport's SHA check.
The matching installed KernelEx auxiliary `MSIMG32.DLL` was independently
retrieved and identified in `GDI_ALPHA_GUEST_BASELINE.md`.

The direct program returned exit 0, `mode=direct-M98GDI1` and
`internal_failures=0x00000000`. Its 14 cases covered opaque/constant/per-pixel
alpha, an untouched neighboring pixel, transparent color key and stretch,
horizontal/vertical/triangle gradients, an invalid gradient mesh index and
mode, invalid HDC and a negative width. Pixel values matched the measured
modern 32-bit oracle for those cases. The adapter changed the two documented
legacy error cases: null destination returned error 6, and an out-of-range
gradient index returned `FALSE` with the incoming last error. This is **direct
provider-table guest evidence**, not static KernelEx import routing or
application functionality. The private receipt is
`build/gdi-alpha/guest-direct-contract.json`; its captured stdout SHA-256 is
`1ff194d802ee1900036cff230bf445c5358432666bf1fb3349c35bebe820ae09`.

The separate `GDIFIX.DLL` fixture ran with no sibling `MSIMG32.DLL` in its
test directory. All three direct function calls returned `FALSE` with Win98
last error `0x00000485` (1157, `ERROR_DLL_NOT_FOUND`), rather than inventing a
successful drawing result. `tests/gdi_alpha_backend_guest_probe.c` records
these values in `build/gdi-alpha/guest-absent-diagnostic.json`. An older host
fixture accepted only error 126 (`ERROR_MOD_NOT_FOUND`), so it reported a
failure in this Win98 missing-backend case even though the provider failed
closed. The diagnostic replaced an earlier conjecture that error 193 would
occur. The guest was then handed to the root task for CORE routing and cold
boot; no more guest commands were issued by this subtask.

The current adapter has deliberate bounds, and this small pixel suite cannot
establish full modern GDI compatibility. Static imports, application launch
and broader pixel/driver behavior require separate evidence.
