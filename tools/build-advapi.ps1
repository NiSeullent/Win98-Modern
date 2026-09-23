# Isolated RegGetValueW API-library build. Host direct and import probes do
# not establish guest KernelEx routing; that validation is separate.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/advapi'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source

$dllFlags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-shared',
    '-Wl,--gc-sections','-Wl,--entry,_DllMain@12',
    '-Wl,--subsystem,windows:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware')
$testFlags = @('-std=c11','-Os','-Wall','-Wextra','-Werror',
    '-fno-builtin','-nostdlib','-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware')

& $compiler @dllFlags '-o' (Join-Path $outDir 'm98advapi.dll') (Join-Path $projectRoot 'src/m98advapi.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'm98advapi.dll build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'advapi_smoke.exe') (Join-Path $projectRoot 'tests/advapi_smoke.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'advapi_smoke.exe build failed' }
& $compiler @testFlags '-o' (Join-Path $outDir 'advapi_import_probe.exe') (Join-Path $projectRoot 'tests/advapi_import_probe.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'advapi_import_probe.exe build failed' }

Push-Location $projectRoot
try {
    & python 'tests/check_m98advapi_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'm98advapi static PE gate failed' }
    if (-not $SkipHostExecution) {
        & (Join-Path $outDir 'advapi_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'RegGetValueW direct host smoke failed' }
        & (Join-Path $outDir 'advapi_import_probe.exe')
        if ($LASTEXITCODE -ne 0) { throw 'RegGetValueW static import host smoke failed' }
    }
} finally {
    Pop-Location
}
