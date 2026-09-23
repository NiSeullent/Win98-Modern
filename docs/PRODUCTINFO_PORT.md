# GetProductInfo compatibility profile

The pinned Notepad++ 8.9.8 x86 image reached a new loader failure at
`KERNEL32.DLL!GetProductInfo` after the `GetTimeFormatEx` static import
passed on the directly installed Windows 98 SE guest. The screenshot is
`vm/npp-after-timeformat.png` (lab evidence, not a shipped artifact).

`src/m98wrap.c` now provides `GetProductInfo` in the sorted KernelEx
`KERNEL32.DLL` API table. On a 6.x or newer requested version it reports
a desktop compatibility SKU: `PRODUCT_ULTIMATE` for 6.0/6.1 and
`PRODUCT_PROFESSIONAL` for 6.2+, including 10.0. Versions below 6 return
`PRODUCT_UNDEFINED` and failure. A NULL output pointer fails. Service-pack
numbers have no SKU effect in this initial profile. The value is an
**emulated API result** for a Win98 application personality, not a claim
that the host is Windows Ultimate/Professional or has those kernel features.

The public [Microsoft GetProductInfo contract](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getproductinfo)
defines a minimum major version of six and product-type mapping. Pinned
[Wine `RtlGetProductInfo`](https://github.com/wine-mirror/wine/blob/df15af3652511150490934682202d45af892f887/dlls/ntdll/version.c)
provided a reference for the `< 6` rejection; pinned
[ReactOS Kernel32 exports](https://github.com/reactos/reactos/blob/9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8/dll/win32/kernel32/kernel32.spec)
forward the API to an NT implementation. The Win98 code is newly written;
neither upstream implementation was copied.

`tools/build-productinfo.ps1` builds the DLL and two probes. Its PE gate
checks the OEM KERNEL32 imports and the static import probe. Both direct
and static host tests passed. The DLL SHA-256 uploaded to the disposable
Win98 guest as `M98WRP3.DLL` was
`cf49f04b1336db90c28669194f59d6995e5d2fff406399cb39ed04cf225b3bde`.
The guest direct behavior test `PRODUCT_SMOKE.EXE` passed. After restoring
`before-uxtheme-known-20260923`, applying **only** `M98WRP3.DLL`, and cold
booting, the static `KERNEL32.GetProductInfo` and `GetTimeFormatEx` probes
both passed. A later cold boot with the experimental UXTHEME KnownDLL also
passed. The first combined **warm restart** had produced a VxD exception and
VirtualBox triple fault; its checkpoint is
`vxd-after-product-theme-20260924`. It was not reproduced by these cold
boots, but repeated warm-restart reliability is still unverified.

Passing a loader import can expose the next unmet API; it is not an
application launch success. No Notepad++ execution success is claimed here.
