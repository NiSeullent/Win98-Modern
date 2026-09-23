# Separate deterministic FlsFree/rundown race probe and optional native oracle.
# Guest execution is an independent gate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/fls-rundown-race'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$common = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--disable-tsaware','-Wl,--no-insert-timestamp')
$dll = Join-Path $outDir 'FLSFIX.DLL'
$exe = Join-Path $outDir 'FLSRACE.EXE'
& $compiler @common '-shared' '-Wl,--entry,_DllMain@12' `
    '-Wl,--subsystem,windows:4.10' '-o' $dll `
    (Join-Path $projectRoot 'src/m98_fls.c') `
    (Join-Path $projectRoot 'tests/fls_fixture.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'FLS race fixture build failed' }
& $compiler @common '-Wl,--entry,_mainCRTStartup' `
    '-Wl,--subsystem,console:4.10' '-o' $exe `
    (Join-Path $projectRoot 'tests/fls_rundown_race.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'FLS race EXE build failed' }

$gate = @'
import json, pathlib, sys
import pefile
dll, exe, manifest = map(pathlib.Path, sys.argv[1:4])
native = set(json.loads(manifest.read_text(encoding="utf-8"))["dlls"]["KERNEL32.DLL"])
for path, expect_dll, subsystem in ((dll, True, 2), (exe, False, 3)):
    pe = pefile.PE(str(path)); opt = pe.OPTIONAL_HEADER
    errors = []
    if pe.FILE_HEADER.Machine != 0x14c or opt.Magic != 0x10b or pe.is_dll() != expect_dll:
        errors.append("expected i386 PE32 with correct DLL/EXE kind")
    if opt.Subsystem != subsystem or (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) != (4, 10):
        errors.append("expected Win98 subsystem 4.10")
    if opt.DllCharacteristics & (0x40 | 0x100):
        errors.append("ASLR or NX enabled")
    if any(opt.DATA_DIRECTORY[i].VirtualAddress for i in (9, 13, 14)):
        errors.append("unexpected TLS, delay import or CLR directory")
    imports = {}
    for desc in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ()):
        names = imports.setdefault(desc.dll.decode("ascii").upper(), set())
        for entry in desc.imports:
            if entry.name is None: errors.append("ordinal import")
            else: names.add(entry.name.decode("ascii"))
    if set(imports) != {"KERNEL32.DLL"}:
        errors.append("unexpected import DLLs " + repr(sorted(imports)))
    if imports.get("KERNEL32.DLL", set()) - native:
        errors.append("unverified Win98 OEM imports " +
                      repr(sorted(imports["KERNEL32.DLL"] - native)))
    pe.close()
    if errors:
        for error in errors: print(f"FAIL: {path.name}: {error}", file=sys.stderr)
        sys.exit(1)
print("PASS: FLS race DLL/EXE PE32, Win98 4.10, OEM-native imports")
'@
$gate | & python - $dll $exe `
    (Join-Path $projectRoot 'benchmarks/win98se-ko-oem-native-exports-v1.json')
if ($LASTEXITCODE -ne 0) { throw 'FLS race PE98 gate failed' }
if (-not $SkipHostExecution) {
    Push-Location $outDir
    try {
        & $exe
        if ($LASTEXITCODE -ne 0) { throw 'FLS race host regression failed' }
    } finally { Pop-Location }
}
Write-Host "Guest handoff: $outDir"
Write-Host "FLSFIX.DLL SHA256: $((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Host "FLSRACE.EXE SHA256: $((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant())"
