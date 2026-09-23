# Build a PE32/Win98 native fiber baseline. Guest execution is separate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/fls-native'
$outExe = Join-Path $outDir 'fls_native_probe.exe'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror',
    '-fno-builtin','-nostdlib','-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp')
& $compiler @flags '-o' $outExe (Join-Path $projectRoot 'tests/fls_native_probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'FLS native probe build failed' }

# Compare every static import to exports extracted from the project's exact
# Korean Win98 SE OEM KERNEL32, so an accidental Vista FLS import fails here.
$gate = @'
import json, pathlib, sys, pefile
binary = pathlib.Path(sys.argv[1])
manifest = pathlib.Path(sys.argv[2])
native = set(json.loads(manifest.read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
pe = pefile.PE(str(binary))
opt = pe.OPTIONAL_HEADER
assert pe.FILE_HEADER.Machine == 0x14c and opt.Magic == 0x10b, "expected i386 PE32"
assert opt.Subsystem == 3 and (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) == (4, 10), "expected console subsystem 4.10"
assert not (opt.DllCharacteristics & (0x40 | 0x100)), "ASLR/NX must be disabled"
assert not any(opt.DATA_DIRECTORY[i].VirtualAddress for i in (9, 13, 14)), "TLS, delay import, or CLR unsupported"
imports = {}
for desc in pe.DIRECTORY_ENTRY_IMPORT:
    dll = desc.dll.decode("ascii").upper()
    imports[dll] = set()
    for entry in desc.imports:
        assert entry.name is not None, f"ordinal import from {dll}"
        imports[dll].add(entry.name.decode("ascii"))
assert set(imports) == {"KERNEL32.DLL"}, f"unexpected import DLLs: {set(imports)}"
missing = imports["KERNEL32.DLL"] - native
assert not missing, f"not in pinned OEM exports: {sorted(missing)}"
assert not (imports["KERNEL32.DLL"] & {"FlsAlloc", "FlsFree", "FlsGetValue", "FlsSetValue"}), "probe must use native fiber/TLS only"
expected = {"CreateThread", "ConvertThreadToFiber", "CreateFiber", "SwitchToFiber", "DeleteFiber", "TlsAlloc", "TlsGetValue", "TlsSetValue", "TlsFree"}
assert expected <= imports["KERNEL32.DLL"], f"missing probe imports: {sorted(expected - imports['KERNEL32.DLL'])}"
print("PASS: FLS native probe PE32/i386, Win98 loader, OEM-only fiber/TLS imports")
'@
$manifest = Join-Path $projectRoot 'benchmarks/win98se-ko-oem-native-exports-v1.json'
$gate | & python - $outExe $manifest
if ($LASTEXITCODE -ne 0) { throw 'FLS native PE/import gate failed' }
if (-not $SkipHostExecution) {
    & $outExe
    if ($LASTEXITCODE -ne 0) { throw 'FLS native host execution failed' }
}
Write-Host "FLS native probe: $outExe"
