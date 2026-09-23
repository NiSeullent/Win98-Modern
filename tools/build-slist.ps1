# Isolated SList family; shared m98wrap/NT integration is a separate gate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/slist'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$common = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp')
$dllFlags = @('-shared','-Wl,--entry,_DllMain@12','-Wl,--subsystem,windows:4.10')
$exeFlags = @('-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,console:4.10')
$module = Join-Path $projectRoot 'src/m98_slist.c'
& $compiler @common @dllFlags '-o' (Join-Path $outDir 'SLISTFIX.DLL') $module (Join-Path $projectRoot 'tests/slist_fixture.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'SList fixture build failed' }
foreach ($name in @('slist_smoke','slist_import_probe','slist_native_probe')) {
    & $compiler @common @exeFlags '-o' (Join-Path $outDir "$name.exe") (Join-Path $projectRoot "tests/$name.c") '-lkernel32'
    if ($LASTEXITCODE -ne 0) { throw "$name build failed" }
}
& $compiler @common @exeFlags '-DM98_INTEGRATED' '-o' (Join-Path $outDir 'slist_integrated_probe.exe') (Join-Path $projectRoot 'tests/slist_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'SList integrated probe build failed' }
& $compiler @common @exeFlags '-DM98_SLIST_TEST_HOOK' '-o' (Join-Path $outDir 'slist_fault_smoke.exe') $module (Join-Path $projectRoot 'tests/slist_fault_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'SList deterministic fault probe build failed' }
Push-Location $projectRoot
try {
    & python 'tests/check_slist_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'SList PE/import gate failed' }
    if (-not $SkipHostExecution) {
        foreach ($name in @('slist_smoke','slist_import_probe','slist_fault_smoke','slist_native_probe')) {
            & (Join-Path $outDir "$name.exe")
            if ($LASTEXITCODE -ne 0) { throw "$name host execution failed: $LASTEXITCODE" }
        }
    }
} finally { Pop-Location }
