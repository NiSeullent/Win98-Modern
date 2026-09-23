# Isolated host build/test for QueryFullProcessImageNameA/W.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/processpath'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source

$dllFlags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-shared',
    '-Wl,--gc-sections','-Wl,--entry,_DllMain@12',
    '-Wl,--subsystem,windows:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp')
$testFlags = @('-std=c11','-Os','-Wall','-Wextra','-Werror',
    '-fno-builtin','-nostdlib','-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp')

$dllSources = @((Join-Path $projectRoot 'src/m98wrap.c'),
    (Join-Path $projectRoot 'src/m98nls_ex.c'),
    (Join-Path $projectRoot 'src/m98_threadpool.c'),
    (Join-Path $projectRoot 'src/m98_initonce.c'),
    (Join-Path $projectRoot 'src/m98_slist.c'),
    (Join-Path $projectRoot 'src/wine_uppercase.c'))
& $compiler @dllFlags '-o' (Join-Path $outDir 'm98wrap.dll') @dllSources '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'processpath_smoke.exe') (Join-Path $projectRoot 'tests/processpath_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'processpath_smoke.exe build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'processpath_import_probe.exe') (Join-Path $projectRoot 'tests/processpath_import_probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'processpath_import_probe.exe build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'smoke.exe') (Join-Path $projectRoot 'tests/smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'smoke.exe build failed' }

Push-Location $projectRoot
try {
    & python 'tests/check_processpath_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'process-path static PE gate failed' }
    if (-not $SkipHostExecution) {
        & (Join-Path $outDir 'processpath_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'process-path direct host smoke failed' }
        & (Join-Path $outDir 'processpath_import_probe.exe')
        if ($LASTEXITCODE -ne 0) { throw 'process-path static host probe failed' }
        & (Join-Path $outDir 'smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'KERNEL32 table host smoke failed' }
    }
} finally {
    Pop-Location
}
