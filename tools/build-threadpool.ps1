# Isolated host/guest-handoff build for five Win98 threadpool-work APIs.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/threadpool'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source

$common = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib',
    '-Wl,--gc-sections','-Wl,--subsystem,windows:4.10',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--disable-tsaware','-Wl,--no-insert-timestamp')
$dllFlags = @('-shared','-Wl,--entry,_DllMain@12')
$exeFlags = @('-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,console:4.10')

& $compiler @common @dllFlags '-o' (Join-Path $outDir 'threadpool_fixture.dll') `
    (Join-Path $projectRoot 'src/m98_threadpool.c') `
    (Join-Path $projectRoot 'tests/threadpool_fixture.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'threadpool fixture DLL build failed' }
& $compiler @common @dllFlags '-Wl,--image-base,0x64000000' `
    '-o' (Join-Path $outDir 'TPMARK.DLL') `
    (Join-Path $projectRoot 'tests/threadpool_marker.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'threadpool marker DLL build failed' }
foreach ($name in @('threadpool_smoke','threadpool_import_probe',
                     'threadpool_relocation_probe')) {
    & $compiler @common @exeFlags '-o' (Join-Path $outDir "$name.exe") `
        (Join-Path $projectRoot "tests/$name.c") '-lkernel32'
    if ($LASTEXITCODE -ne 0) { throw "$name.exe build failed" }
}
& $compiler @common @exeFlags '-DM98_INTEGRATED' '-o' `
    (Join-Path $outDir 'threadpool_integrated_probe.exe') `
    (Join-Path $projectRoot 'tests/threadpool_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'threadpool integrated probe build failed' }
# This full contract probe must run in the Win98 guest. Its explicit rejection
# checks for unsupported custom pools are specific to this provider.
& $compiler @common @exeFlags '-DM98_STATIC' '-o' `
    (Join-Path $outDir 'threadpool_guest_import_smoke.exe') `
    (Join-Path $projectRoot 'tests/threadpool_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'threadpool guest static smoke build failed' }

Push-Location $projectRoot
try {
    & python 'tests/check_threadpool_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'threadpool PE98 static gate failed' }
} finally {
    Pop-Location
}
if (-not $SkipHostExecution) {
    Push-Location $outDir
    try {
        & (Join-Path $outDir 'threadpool_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'threadpool direct host smoke failed' }
        & (Join-Path $outDir 'threadpool_import_probe.exe')
        if ($LASTEXITCODE -ne 0) { throw 'threadpool static host probe failed' }
        & (Join-Path $outDir 'threadpool_relocation_probe.exe')
        if ($LASTEXITCODE -ne 0) { throw 'threadpool marker relocation probe failed' }
    } finally {
        Pop-Location
    }
}
Write-Host "Threadpool test binaries: $outDir"
