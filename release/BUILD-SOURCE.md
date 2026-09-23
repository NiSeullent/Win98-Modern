# DLL source and build instructions

This patch ZIP includes corresponding C source, export definitions, build
scripts, PE validation scripts, and license notices for `m98wrap.dll`,
`m98shell.dll`, `m98adv.dll`, `UXTHEME.DLL`, `dbghelp.dll`, `dwmapi.dll`,
`bcrypt.dll`, and the guarded `KSWITCH.EXE` helper. It contains **three exact
KernelEx UXTHEME source files**, not the KernelEx installer or full repository.
It does not include Windows, Microsoft Unicode Layer, any target application,
installation keys, test VM images, or remote guest state.

The combined `UXTHEME.DLL` uses pinned KernelEx commit
`31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`. The original `uxtheme.c`,
`metric.c`, and `uxtheme.def` are included at
`third_party/KernelEx/auxiliary/uxtheme/` with their original notices.
`KERNELEX-UXTHEME-NOTICES.md` records their hashes and licenses. The build
script **refuses a hash mismatch** before compiling them, and needs no Git
submodule when run from the extracted ZIP. `LICENSE` supplies GPL v2;
`licenses/Wine-LGPL-2.1.txt` supplies LGPL v2.1 for the upstream `metric.c`
and Wine-derived material.

The `skills/win98-modern-lab/` folder is a reusable Codex workflow for the
entire project. Copy that folder into your Codex personal skills directory to
use it across tasks; it is also maintained in the public source repository.

On a modern Windows host, install 32-bit LLVM MinGW with
`i686-w64-mingw32-gcc` on `PATH`, Python, and PowerShell 7.2 or newer. The
LLVM MinGW toolchain is required because the pinned KernelEx UXTHEME source
uses Microsoft-style assembly blocks compiled with `-fasm-blocks`. Install the PE
validation dependency with:

```powershell
python -m pip install -r tests/requirements.txt
```

Then run this from the extracted ZIP directory:

```powershell
.\build-dlls.ps1
```

The binaries are written to `build/`; `build/uxtheme-known/UXTHEME.DLL` is
the combined KnownDLL candidate, and `build/m98adv.dll` is the DOS 8.3 copy
of the ADVAPI API library. The script checks PE32/Win98 loader headers,
base relocations, original-media native imports, UXTHEME's original 48 plus
six added exports, and the guarded mapping helper. Its UXTHEME checker uses
the pinned target app's **import names only**; that app is not inside the ZIP.
At this preview snapshot `m98wrap.dll` has a 62-name KernelEx KERNEL32 table.
This is the number of registered names, not a whole-Windows-API compatibility
percentage or a claim that all 62 behaviors are complete.

`build-dlls.ps1` also runs the included focused builds for final-path, stream,
locale information, application restart, process path, SRW condition
variables, named-locale NLS, threadpool work, and one-time initialization. The first six focused
builds compile the production wrapper and must be byte-identical to the
`m98wrap.dll` shipped in this ZIP. The NLS, threadpool, and InitOnce builds use separate
fixture DLLs for direct API-table tests, plus static-import probes; those
fixtures are not interchangeable with the production wrapper. All three also
compile integrated probes for a direct production-wrapper test in the guest.
The integrated probes pass PE/import checks but their guest execution must be
recorded separately. All nine builds run their Win98 PE/import checks and
focused host tests. The matching source,
test files, and bounded evidence documents are in the ZIP, including
`docs/NLS_EX_PORT.md`, `docs/THREADPOOL_WORK_PORT.md`, and `docs/INITONCE_PORT.md`. Host execution,
isolated guest fixture execution, production KernelEx static-import tests,
and application launch are separate verification stages. These checks are
not a claim that every table entry or modern application works.

The InitOnce batch contains `InitOnceInitialize`, `InitOnceBeginInitialize`,
`InitOnceComplete`, and `InitOnceExecuteOnce`, sharing the same state machine.
The build runs the fixture and the host's original static-import API through
their common valid-use contracts and 192 worker executions, then runs the
integrated probe against the production `build/m98wrap.dll`. This establishes
host behavior; installed Win98 KernelEx import evidence is recorded separately
in `docs/INITONCE_PORT.md`. Invalid ExecuteOnce usage has an explicit difference:
an active asynchronous object or a callback returning a misaligned context
produces FALSE / `ERROR_INVALID_PARAMETER` in this preview, whereas the native
host terminates the isolated invalid-use probes with `0xC00000F0` or `0xC00000F1`.
This preview does not claim identical invalid-use exception behavior.

`guest-tests/` includes the compiled `initonce_import_probe.exe`,
`threadpool_guest_import_smoke.exe`, and the latter's companion `TPMARK.DLL`.
Keep that DLL beside the threadpool executable and use the guest-tests directory
as its working directory. The threadpool suite tests this provider's unsupported
custom-environment rejections, so it is intended for the installed Win98 provider,
not the host's full modern threadpool. These files are test tools; including them
in the ZIP does not assert a guest pass for the packaged snapshot. Both executable
probes and the marker are covered by the package checksums and can be rebuilt
from the included source.

Generated file hashes can differ across compiler versions. The ZIP's
`SHA256SUMS.txt` records files actually shipped, not locally rebuilt output.

In the source checkout, create a release ZIP after building and validating:

```powershell
.\release\build-dlls.ps1
.\tools\package-release.ps1 -Version 0.1.3-preview
```

Replace `0.1.3-preview` with the chosen release version. The packaging script writes
the ZIP and its `.sha256` file under `build/releases/` and selects every ZIP
member from a fixed list.
