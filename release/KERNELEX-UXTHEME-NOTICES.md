# KernelEx UXTHEME source and license notices

`UXTHEME.DLL` in this patch ZIP is built from three unmodified files from
KernelEx commit
[`31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`](https://github.com/metaxor/KernelEx/tree/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1)
and two new Win98-Modern files. The build script verifies the upstream file
hashes before compilation, including when the ZIP is extracted without Git.

| Source file in this ZIP | SHA-256 | License |
| --- | --- | --- |
| `third_party/KernelEx/auxiliary/uxtheme/uxtheme.c` | `1df5ae49d5545f756cca950842809443ada4b13dd980ac6a67ef686737be1020` | GPL-2.0-only, original copyright and notice retained in file |
| `third_party/KernelEx/auxiliary/uxtheme/metric.c` | `dc6718b383230fc948907ab95d5376e476727b5c29639df29dcff59ee39f5ec6` | LGPL-2.1-or-later, original copyright and notice retained in file |
| `third_party/KernelEx/auxiliary/uxtheme/uxtheme.def` | `bf43b22625e5ee24c3727fd6b0d1bf51116705abfbb197f95db962889d0b446f` | Distributed with the GPL-2.0 KernelEx UXTHEME source |
| `src/uxtheme_shim.c` | See `SHA256SUMS.txt` | New GPL-2.0-only implementation |
| `src/uxtheme_sysfont.c` | See `SHA256SUMS.txt` | New GPL-2.0-only implementation |

The full GPL v2 license text is `LICENSE`. The full LGPL v2.1 text is
`licenses/Wine-LGPL-2.1.txt`; that license text applies equally to KernelEx's
`metric.c`, despite the file's historical name in this repository. Per-file
notices and this file should stay with redistributions of the combined DLL.

The upstream `.def` is read and rewritten into a generated file in `build/`
to retain all original exports, redirect selected no-theme functions to the
new implementations, and add six exports. The generated definition is a
build artifact and can be regenerated using `build-dlls.ps1`.
