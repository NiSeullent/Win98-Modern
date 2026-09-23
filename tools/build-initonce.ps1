# Isolated Win98 InitOnce fixture; integration into m98wrap is a separate gate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/initonce'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$common = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp')
$dllFlags = @('-shared','-Wl,--entry,_DllMain@12','-Wl,--subsystem,windows:4.10')
$exeFlags = @('-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,console:4.10')
& $compiler @common @dllFlags '-o' (Join-Path $outDir 'ONCEFIX.DLL') (Join-Path $projectRoot 'src/m98_initonce.c') (Join-Path $projectRoot 'tests/initonce_fixture.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'InitOnce fixture build failed' }
& $compiler @common @exeFlags '-o' (Join-Path $outDir 'initonce_smoke.exe') (Join-Path $projectRoot 'tests/initonce_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'InitOnce direct smoke build failed' }
& $compiler @common @exeFlags '-o' (Join-Path $outDir 'initonce_import_probe.exe') (Join-Path $projectRoot 'tests/initonce_import_probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'InitOnce static import probe build failed' }
& $compiler @common @exeFlags '-DM98_INTEGRATED' '-o' (Join-Path $outDir 'initonce_integrated_probe.exe') (Join-Path $projectRoot 'tests/initonce_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'InitOnce integrated probe build failed' }
Push-Location $projectRoot
try {
    & python 'tests/check_initonce_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'InitOnce PE/import gate failed' }
    if (-not $SkipHostExecution) {
        & (Join-Path $outDir 'initonce_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'InitOnce fixture behavior failed' }
        & (Join-Path $outDir 'initonce_import_probe.exe')
        if ($LASTEXITCODE -ne 0) { throw 'InitOnce native host comparison failed' }
    }
} finally { Pop-Location }
