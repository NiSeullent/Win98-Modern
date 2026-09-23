# Isolated host build/test of the two NLS Ex bridges. Guest tests are separate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build/nls-ex'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source

$sharedFlags = @('-std=c11','-Os','-Wall','-Wextra','-Werror',
    '-fno-builtin','-ffunction-sections','-fdata-sections','-nostdlib',
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

$dllArgs = @('-shared','-o',(Join-Path $outputDir 'nls_ex_fixture.dll'),
    (Join-Path $projectRoot 'src/m98nls_ex.c'),
    (Join-Path $projectRoot 'tests/nls_ex_fixture.c'),'-lkernel32')
& $compiler @sharedFlags @dllArgs
if ($LASTEXITCODE -ne 0) { throw 'NLS Ex fixture DLL build failed' }
foreach ($name in @('nls_ex_smoke','nls_ex_native_probe','nls_ex_import_probe')) {
    $testArgs = @('-o',(Join-Path $outputDir "$name.exe"),
        (Join-Path $projectRoot "tests/$name.c"),'-lkernel32')
    & $compiler @testFlags @testArgs
    if ($LASTEXITCODE -ne 0) { throw "$name build failed" }
}
& $compiler @testFlags '-DM98_INTEGRATED' '-o' `
    (Join-Path $outputDir 'nls_ex_integrated_probe.exe') `
    (Join-Path $projectRoot 'tests/nls_ex_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'nls_ex_integrated_probe build failed' }
& python (Join-Path $projectRoot 'tests/check_nls_ex_pe98.py')
if ($LASTEXITCODE -ne 0) { throw 'NLS Ex PE/import gate failed' }
if (-not $SkipHostExecution) {
    & (Join-Path $outputDir 'nls_ex_smoke.exe')
    if ($LASTEXITCODE -ne 0) { throw 'NLS Ex direct host smoke failed' }
}
Write-Host "NLS Ex test binaries: $outputDir"
