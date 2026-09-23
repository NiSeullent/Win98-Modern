# Build a diagnostic using only original Win98 KERNEL32 imports.
# Guest execution is a separate evidence gate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/thread-lifecycle'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir 'THRLIFE.EXE'
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib',
    '-Wl,--gc-sections','-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp')
& $compiler @flags '-o' $out `
    (Join-Path $projectRoot 'tests/thread_lifecycle_probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'thread lifecycle probe build failed' }

# Keep the gate with this one-purpose build tool; no new shared test files.
$peGate = @'
import json, pathlib, sys
import pefile
exe, manifest = map(pathlib.Path, sys.argv[1:3])
native = set(json.loads(manifest.read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
pe = pefile.PE(str(exe))
opt = pe.OPTIONAL_HEADER
errors = []
if pe.FILE_HEADER.Machine != 0x14c or opt.Magic != 0x10b or pe.is_dll():
    errors.append("expected i386 PE32 EXE")
if opt.Subsystem != 3 or (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
    errors.append("expected console subsystem 4.10")
if opt.DllCharacteristics & (0x0040 | 0x0100):
    errors.append("ASLR or NX enabled")
for index, label in ((9, "TLS"), (13, "delay import"), (14, "CLR")):
    if opt.DATA_DIRECTORY[index].VirtualAddress:
        errors.append("unexpected " + label + " directory")
imports = {}
for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
    dll = descriptor.dll.decode("ascii").upper()
    names = imports.setdefault(dll, set())
    for item in descriptor.imports:
        if item.name is None:
            errors.append("ordinal import from " + dll)
        else:
            names.add(item.name.decode("ascii"))
if set(imports) != {"KERNEL32.DLL"}:
    errors.append("unexpected import DLLs: " + repr(sorted(imports)))
names = imports.get("KERNEL32.DLL", set())
if names - native:
    errors.append("not in original Win98 OEM manifest: " + repr(sorted(names - native)))
required = {"CreateThread", "ExitThread", "FreeLibraryAndExitThread",
            "GetProcAddress", "LoadLibraryA", "VirtualQuery"}
if required - names:
    errors.append("missing diagnostic imports: " + repr(sorted(required - names)))
pe.close()
if errors:
    for error in errors: print("FAIL: thread lifecycle PE98 gate: " + error, file=sys.stderr)
    sys.exit(1)
print("PASS: thread lifecycle PE32/i386, Win98 4.10, original OEM imports")
'@
$manifest = Join-Path $projectRoot 'benchmarks/win98se-ko-oem-native-exports-v1.json'
$peGate | & python - $out $manifest
if ($LASTEXITCODE -ne 0) { throw 'thread lifecycle PE98 gate failed' }

if (-not $SkipHostExecution) {
    & $out
    if ($LASTEXITCODE -ne 0) { throw 'thread lifecycle host diagnostic failed' }
}
$hash = (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Guest handoff: $out"
Write-Host "SHA256: $hash"
