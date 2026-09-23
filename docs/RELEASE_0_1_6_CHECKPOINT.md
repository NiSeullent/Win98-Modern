# 0.1.6-preview: USER32 clipboard and catalogue route checkpoint

The complete Windows API and five-application goal remains active. This
checkpoint adds a separate USER32 provider, repairs the source catalogue's
project DLL identity, and makes family route installation DLL-specific.

## Local patch and source rebuild

- Archive: `build/releases/win98-modern-0.1.6-preview-x86.zip`.
- Size: 827,162 bytes; SHA-256:
  `48c8ca7a9764d55f1f35a5462269162370657613011cd5bb17385e76cefac393`.
- Independent `build/release-source-check-016-receipt.json` verified 185/185 ZIP
  CRC entries, all 184 manifest hashes and the exact allowlist. No Windows
  media, product key, VM image, selected app, site output or secret-shaped
  member was found.
- The auditor extracted 152 source/metadata files while omitting all 33
  DLL/EXE files. The extracted `build-dlls.ps1` rebuilt all 33 binaries
  byte-identically: zero mismatch, zero missing rebuild and no extracted input
  mutation. Full log: `build/release-source-check-016-build.log`.
- This is a local patch/source preview. It is not a Windows installation image.

## Directly installed Windows 98 SE

The guest `Win98Modern-Accel-128` was cold-booted after installing the new
versioned USER32 provider. The current VirtualBox log and the suite recorded
NEM hardware partition creation. `M98USR1.DLL` (same bytes as release
`M98USER.DLL`) has SHA-256
`4932584fcb7f1af493dbebfd76a38ede4f04b5543a0559cdd8fd1c33ff8b3fc7`.
The installed CORE.INI SHA-256 was
`2e853bcdd3a686ad8d105ee47219049efcdd9fbcfeddeac29c9d061eadd0bcc9`.
It added three named USER32 functions to each of three active profiles, nine
routes in total, while preserving the existing KERNEL32 route bytes.

`remote/suites/api-clipboard.json` passed **6/6** guest tests with exit 0 and
no timeout; receipt `build/guest/suite-clipboard-user1.json`. The suite
verified direct fixture and shipping DLL calls, three real static USER32
imports, and a full static-import contract test. Owned CF_TEXT and
CF_UNICODETEXT content was read back, native and bridge format IDs agreed,
six queued clipboard changes delivered six modern and six legacy viewer
notifications, and the initially empty clipboard was restored.

An independent reviewer checked ABI, process ownership, worker lifetime,
viewer-chain reentry and the route helper. A Win98-specific silent
EnumClipboardFormats result was reproduced with the original DLL and repaired
before promotion. The supported scope and remaining lifetime limits are in
`docs/CLIPBOARD_PORT.md` and `docs/CLIPBOARD_CONTRACT_TESTS.md`.

## Catalogue and selected applications

The refreshed catalogue has 385,943 records: 151,330 SDK candidates and
234,613 supplemental records. All are assigned among 8,002 batches. The
clipboard batch has 45 entries: 23 USER32 names and 22 WIN32U names. Three
USER32 entries have new project declarations and current static guest contract
receipts, raising focused guest subset entries to 41. Its whole-API
compatibility percentage remains `null`; the batch is not complete.

Notepad++ 8.9.8 x86 still fails to start. Its first displayed loader error
after the clipboard provider is `GDI32.DLL!GdiAlphaBlend` rather than the
earlier USER32 clipboard listener name. The exact provider, receipt and
screenshot hashes are in `benchmarks/npp-clipboard-user1.json`. Chromium 150,
Supermium and VLC retain their earlier failed-functionality results; the
selected VS Code 1.138.0 binary is x64 and still needs an architecture path.
No selected application has a functionality pass.

The next work unit is the GDI alpha drawing family and its common backend,
based on the complete catalogue and the pinned Wine/ReactOS/KernelEx sources.
Win98 native `MSIMG32.DLL` exports are candidate dependencies; export names
alone are not proof of pixel behavior or an application launch.
