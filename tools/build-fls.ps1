# Independent FLS/fiber lifecycle fixture. Guest execution is a separate gate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/fls'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$common = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--disable-tsaware','-Wl,--no-insert-timestamp')
$dllFlags = @('-shared','-Wl,--entry,_DllMain@12','-Wl,--subsystem,windows:4.10')
$exeFlags = @('-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,console:4.10')
& $compiler @common @dllFlags '-o' (Join-Path $outDir 'FLSFIX.DLL') `
    (Join-Path $projectRoot 'src/m98_fls.c') `
    (Join-Path $projectRoot 'tests/fls_fixture.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'FLS fixture build failed' }
& $compiler @common @exeFlags '-o' (Join-Path $outDir 'fls_smoke.exe') `
    (Join-Path $projectRoot 'tests/fls_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'FLS smoke build failed' }
Push-Location $projectRoot
try {
    & python 'tests/fls_check_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'FLS PE/import gate failed' }
    if (-not $SkipHostExecution) {
        & (Join-Path $outDir 'fls_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'FLS fixture behavior failed' }
    }
} finally { Pop-Location }
Write-Host "FLS fixture: $outDir"
