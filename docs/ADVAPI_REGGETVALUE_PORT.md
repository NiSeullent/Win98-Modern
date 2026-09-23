# ADVAPI32 `RegGetValueA/W` port

This port provides two KernelEx API-library routes for the Windows 98 SE
`ADVAPI32.DLL!RegGetValueA/W` imports. Notepad++ 8.9.8 directly imports W;
A broadens the general registry API surface. Neither import result proves
that Notepad++ starts.

## Source and contract review

- [Microsoft `RegGetValueA`](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluea) and [`RegGetValueW`](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew) define the seven-argument `WINAPI` ABI, type restrictions, subkey/default-value selection, byte-size query, string termination, and failure zeroing.
- Pinned [Wine `dlls/kernelbase/registry.c`, commit `df15af3652511150490934682202d45af892f887`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/kernelbase/registry.c#L1687-L1961) was inspected for A/W type filtering, ANSI terminator repair, expansion, and retrying values that grow between registry queries. Its WOW64 redirection and NT registry calls require infrastructure Win98 does not have.
- Pinned [ReactOS `dll/win32/advapi32/reg/reg.c`, commit `9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8`](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/advapi32/reg/reg.c#L1727-L1982) was inspected for A/W type and expansion behavior. It uses NT Unicode registry access and lacks a Win98 ANSI-store path.
- Both inspected upstream files carry LGPL-2.1-or-later notices. `src/m98advapi.c` is an independent Win98 implementation under this project's GPL-2.0-only license; no upstream source text or data was copied.

The supplied Korean Win98 SE OEM export manifest records original `ADVAPI32.DLL`
SHA-256 `6ce58d7352843ac51695f4f3ec0c78830d0eb2ba9071d16a9836fc083083053d`.
It exports `RegOpenKeyExA`, `RegQueryValueExA`, and `RegCloseKey`, but no
`RegGetValueA/W`. The original `KERNEL32.DLL` exports the heap, code-page, and
ANSI environment expansion functions imported by the new DLL. Native Win98 W
registry exports are present by name but may be compatibility stubs; this port
does not depend on them.

## Win98 behavior

- Null or empty subkeys query the supplied key. A nonempty subkey is opened
  with `KEY_QUERY_VALUE` and always closed. Null or empty value names address
  the unnamed value.
- Unicode key/value names are converted to the active ANSI code page and
  round-tripped before access. An unrepresentable name fails with
  `ERROR_NO_UNICODE_TRANSLATION` instead of silently targeting another name.
  This reflects Win98's ANSI registry storage; arbitrary NT Unicode registry
  names cannot be represented.
- Native string data (`REG_SZ`, `REG_EXPAND_SZ`, `REG_MULTI_SZ`) is converted
  from the active ANSI code page to UTF-16. Binary, DWORD, QWORD, and other
  types retain their exact original bytes. Missing string NULs are appended;
  `REG_MULTI_SZ` is returned with two trailing UTF-16 NULs. The returned byte
  count includes those terminators. The A route keeps Win98's ANSI registry
  bytes and repairs one or two trailing ANSI NULs as the type requires.
- `REG_EXPAND_SZ` uses native `ExpandEnvironmentStringsA` and returns
  `REG_SZ`, unless `RRF_NOEXPAND` is set. Type restrictions are applied to the
  resulting type. `RRF_RT_DWORD` and `RRF_RT_QWORD` accept matching binary
  values only at 4 and 8 bytes, respectively. A zero type mask admits no
  type, matching the pinned Wine/ReactOS behavior.
- The two WOW64 subkey selectors together return `ERROR_INVALID_PARAMETER`.
  Win98 has one 32-bit registry view, so either selector alone addresses that
  same view. No 64-bit registry or redirection is emulated.
- The result is buffered before copying into caller memory. Short buffers
  receive `ERROR_MORE_DATA` and a required byte count; `RRF_ZEROONFAILURE`
  zeroes only the caller's original buffer capacity. A changing registry
  value is reread up to eight times.

The implementation has no multi-user NT access-control emulation, registry
virtualization, WOW64 redirection, or MUI value expansion. Those are separate
platform capabilities. Conversion of malformed ANSI registry string bytes is
subject to Win98's active code-page behavior.

## Verification

Run `tools/build-advapi.ps1`. It builds `build/advapi/m98advapi.dll`, a direct
API-table smoke executable, and an executable with *static*
`ADVAPI32.DLL!RegGetValueA/W` imports. The static PE gate checks loader version,
relocations, DLL imports against the original-media export manifest, and both
static probe import names.

On the Windows development host, the direct smoke test passed size-only reads,
UTF-16 output, omitted string terminator repair, type restrictions, binary and
DWORD data, expansion and `NOEXPAND`, the unnamed value, relative subkeys,
WOW64 flag validation, missing-value errors, buffer zeroing, and four
concurrent readers. The new A direct test covers unterminated strings, type
and binary results, expansion, `NOEXPAND`, bounded failure zeroing, a
multi-string terminator, and conflicting WOW64 selectors. The A/W static
import probe also passed **on the host**, where it resolves the modern system
ADVAPI32 rather than KernelEx.

The guest library was installed under the DOS 8.3 name `M98ADV.DLL`, with
`m98adv` added to the installed KernelEx `CORE.INI` contents and explicit
`RegGetValueW=m98adv.0` routes for Win98, Me, and XP profiles. The original
config was downloaded and hashed before a byte-preserving patch; the updated
file's SHA-256 was `405472922fa025a01378a368b579e690e886872dddbd188024cf7904fdc37b88`.
On 2026-09-24 the directly installed Windows 98 SE guest cold-booted to its
GUI, then the **direct API-table behavior smoke and static ADVAPI32 import
probe both passed** through COM1. This proves the tested subset, not complete
ADVAPI32 compatibility or Notepad++ startup. The source-tree
`integration/core.ini` records the same canonical route; the versioned
test guest also contains unrelated Shell, KERNEL32, and UXTHEME trials.

On 2026-09-24 the new A/W DLL SHA-256
`c1350e6e107025d4e6a790ca4d9a85c2e67afe65f7828982d2f871096bf6bb6e`
passed `PASS: RegGetValueA/W direct API-table registry behavior` in the same
installed guest. It was installed as versioned `M98AD2.DLL`; `CORE.INI` was
patched byte-preservingly from `m98adv` to `m98ad2` with both routes,
SHA-256 `cc60b00bba1af67bc43761272c36907f88b90ec868fe0cc514e647b58a792b0e`.
After a normal shutdown and hardware-accelerated cold boot, the guest returned
`PASS: static ADVAPI32 RegGetValueA/W imports and values` (exit code 0).
This confirms the tested behavior and routing, not complete ADVAPI32
compatibility or Notepad++ startup.
