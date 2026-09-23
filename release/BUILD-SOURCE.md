# DLL source and build instructions

This ZIP includes the corresponding C source, export definition, build script,
PE validation scripts, and license notices for `m98wrap.dll`, `dbghelp.dll`,
`dwmapi.dll`, and `bcrypt.dll`. It does not include Windows, KernelEx, Microsoft Unicode Layer,
or any target application.

The `skills/win98-modern-lab/` folder is a reusable Codex workflow for the
entire project. Copy that folder into your Codex personal skills directory to
use it across tasks; it is also maintained in the public source repository.

On a modern Windows host, install 32-bit LLVM MinGW with
`i686-w64-mingw32-gcc` on `PATH`, Python, and PowerShell 7.2 or newer. Install the PE
validation dependency with:

```powershell
python -m pip install -r tests/requirements.txt
```

Then run this from the extracted ZIP directory:

```powershell
.\build-dlls.ps1
```

The binaries are written to `build/`. The script uses the same DLL compiler
flags and PE validation gates as the repository's `build.ps1`. Generated file
hashes can differ across compiler versions. The ZIP's `SHA256SUMS.txt` records
the files actually shipped, not hashes of locally rebuilt output.

In the source checkout, create a release ZIP after building and validating:

```powershell
.\build.ps1
.\tools\package-release.ps1 -Version 0.1.0
```

Replace `0.1.0` with the chosen release version. The packaging script writes
the ZIP and its `.sha256` file under `build/releases/` and selects every ZIP
member from a fixed list.
