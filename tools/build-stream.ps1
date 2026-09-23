# Isolated build and host checks for the bounded FindFirstStreamW port.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/stream'
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

$dllSources = @(& (Join-Path $PSScriptRoot 'm98wrap-sources.ps1') -ProjectRoot $projectRoot)
& $compiler @dllFlags '-o' (Join-Path $outDir 'm98wrap.dll') @dllSources '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'stream_smoke.exe') (Join-Path $projectRoot 'tests/stream_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'stream_smoke.exe build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'stream_import_probe.exe') (Join-Path $projectRoot 'tests/stream_import_probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'stream_import_probe.exe build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'smoke.exe') (Join-Path $projectRoot 'tests/smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'smoke.exe build failed' }

Push-Location $projectRoot
try {
    & python 'tests/check_stream_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'FindFirstStreamW static PE gate failed' }
    if (-not $SkipHostExecution) {
        & (Join-Path $outDir 'stream_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'FindFirstStreamW direct host smoke failed' }
        & (Join-Path $outDir 'stream_import_probe.exe')
        if ($LASTEXITCODE -ne 0) { throw 'FindFirstStreamW static host probe failed' }
        & (Join-Path $outDir 'smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'KERNEL32 table host smoke failed' }
    }
} finally {
    Pop-Location
}
