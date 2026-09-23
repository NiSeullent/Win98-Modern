# 0.1.8-preview checkpoint: COMCTL32 ordinals and PNG icons

This document describes a candidate patch for an existing, user-installed
Windows 98 SE guest with KernelEx 4.5.2. The Windows image, installation key,
KernelEx installer, target application binaries and VM disk are not part of
the source or release ZIP. It does not establish whole-Windows-API coverage or
successful operation of any complete target application.

## Candidate change

`M98CTLP.DLL` supplies two KernelEx-routed `COMCTL32` ordinals:

| Ordinal | Contract in this candidate | Limit |
| --- | --- | --- |
| 381 | `LoadIconWithScaleDown`, including bounded PNG resources inside icon groups | Selected sizes and icon forms only; not all high-DPI, malformed-resource or display-color-depth ties |
| 345 | `TaskDialogIndirect` with ordinary `MessageBoxA` button sets | No custom buttons, radio/verification controls, icons, callbacks, progress, links, or full TaskDialog UI |

The source and exact test design are in [`COMCTL_ORDINAL_PORT.md`](COMCTL_ORDINAL_PORT.md).
The implementation preserves the installed OEM `COMCTL32.DLL` and places the
candidate under a versioned name, `M98CTL3.DLL`, in the KernelEx folder. The
source ZIP contains the original project bridge and the pinned LodePNG C
decoder with its own license notice. No Wine or ReactOS implementation body
was copied for this family; their icon and common-control sources informed
the design review.

## Direct Windows 98 evidence

The final tested PNG candidate has SHA-256
`aafd97b71ff63c9f9cedffa4705713243d5708f22ff6ffaf04c732261ca0e3f8`.
It passed its PE/OEM-import gate and generated-resource host probes. An exact
copy then returned exit 0 on the installed Windows 98 SE guest for direct
provider-table tests at 16, 32, 40 and 48 pixels. A separate direct probe
decoded the actual PNG-backed Notepad++ 8.9.8 icon group 501 at 16 pixels.
The new same-size color-depth probe ran with a four-bit display and selected
the 16-bpp red source as expected. The guest COM1 receipts are kept locally
as `build/comctl-png/guest-v3-direct.json`,
`build/comctl-png/guest-v3-npp501-direct.json`, and
`build/comctl-png/guest-v3-depth-direct.json`. The icon 501 resource itself
comes from a user-supplied, SHA-verified app executable and is not
redistributed.

After installing this candidate as `M98CTL3.DLL`, migrating the current guest
`CORE.INI` and cold-booting the VM with hardware virtualization, the genuine
`COMCTL32` ordinal-only import probe returned exit 0. It checked both ordinal
IAT slots, an invalid-argument result for 345, and a basic 16-pixel 381 call.
The receipt is `build/comctl-png/guest-v3-static.json`; the guest `CORE.INI`
readback has SHA-256
`eeaa555431c3d31d997afa0d0dee1abcd667c4afcbce2dcabcf56df3317cabea`.
This static probe did
not call the PNG resource path, which was checked by the separate direct
probes. The 345 MessageBox user-interface branch has not been interactively
verified in this guest.

The guarded upgrade tool reads a fresh `CORE.INI` copy, verifies its hash
and the provider's pinned hash, writes an unchanged backup, and changes only
the `m98ctl1` or `m98ctl2` registration and six ordinal routes to `m98ctl3`.
The tested guest upgrade changed seven token references from `m98ctl2` to
`m98ctl3` with a verified readback. An initial
installation uses `tools/patch_core_ordinals.py` instead. Both produce a new
candidate file; neither edits the guest directly. Follow the backup, readback,
cold-boot and rollback steps in `INSTALL-ko.md` at the extracted ZIP root
(or `release/INSTALL-ko.md` in the source checkout).

## Application boundary

The pinned Notepad++ 8.9.8 x86 binary had previously stopped at a
`WM_CREATE` access violation; the first ordinal provider advanced it to an
icon 501 warning. With the final `M98CTL3.DLL` candidate installed, a bounded
`APP_PROBE.EXE` run saw a visible editor by 3.974 seconds and still saw it
at 12.004 seconds, without the icon 501 warning. The probe then posted
`WM_CLOSE` and had to terminate the child. The local receipt is
`build/comctl-png/guest-v3-npp-app.json`. This establishes startup and a
visible editor for v3, not v3 editing, saving, or a clean exit.

With this same final `M98CTL3.DLL` candidate installed, a separate interactive
guest test opened an existing 13-byte `C:\M98LAB\NPPV3.TXT`, edited it, and
pressed `Ctrl+S`. The remote tester then read back 34 bytes with SHA-256
`a1853e54f7e131411c6f0787e80d8f7157f3e60ce85438ddfc427c5b82bf1c2c`:
the exact ASCII content was `Saved under M98CTL3\r\nBaseline v3\r\n`.
The local upload and readback receipts are
`build/comctl-png/guest-v3-save-input-upload.json` and
`build/comctl-png/guest-v3-save-read.json`. This verifies one bounded
existing-file open, edit, save and content round trip for the shipping
candidate. It does not exercise new-document Save As, multiple encodings,
write errors, or clean shutdown. A close operation still displayed a
Windows 98 invalid-page-fault dialog.

Saving a **new** document did not show a Save As dialog or produce a new file.
Notepad++'s pinned source uses the Vista `CLSID_FileSaveDialog` through
`CoCreateInstance` for this operation. A read-only probe on the same guest
found the class keys absent and returned `0x80040154`
(`REGDB_E_CLASSNOTREG`) for the save class and its `IFileDialog` interface,
while a Shell Link COM control succeeded. Thus this guest has a concrete
save-dialog activation blocker. The exact probe output is retained in the
local guest receipt; the experimental COM replacement is outside this ZIP.
No new-file Save As, general file-save coverage, or clean close is claimed.

## Publication gate

The versioned public ZIP checksum, source-only extracted rebuild, and
independent public-download check must be recorded only after this tested
candidate is packaged and published. The SHA above identifies the proposed
shipping `M98CTLP.DLL`, subject to a byte-identical source-package rebuild.
The published 0.1.7-preview remains a separate GDI32
checkpoint and does not contain this COMCTL32 provider.

The full Windows API target, the five-application functional target,
ShizukuDOS as the Windows 98 boot replacement, OS multicore, PAE integration,
ShizukuFS driver, and modern storage/USB drivers remain open work. API
catalogue entries and successful bounded probes are not a compatibility
percentage.
