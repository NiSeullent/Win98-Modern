# Independent clipboard contract probes. Native host mode never mutates data.
param(
    [switch]$SkipHostExecution,
    [string]$FixturePath = '',
    [string]$ProviderPath = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/clipboard-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @(
    '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
    '-ffunction-sections', '-fdata-sections', '-nostdlib',
    '-Wl,--gc-sections', '-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10', '-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10', '-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat', '-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp'
)
$smoke = Join-Path $outDir 'clipboard_smoke.exe'
$static = Join-Path $outDir 'clipboard_import_probe.exe'
$staticSuite = Join-Path $outDir 'clipboard_static_suite.exe'
& $compiler @flags '-o' $smoke (Join-Path $projectRoot 'tests/clipboard_smoke.c') '-luser32' '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'clipboard smoke build failed' }
& $compiler @flags '-o' $static (Join-Path $projectRoot 'tests/clipboard_import_probe.c') '-luser32' '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'clipboard static-import build failed' }
& $compiler @flags '-DM98_STATIC_CLIPBOARD_IMPORTS=1' '-o' $staticSuite (Join-Path $projectRoot 'tests/clipboard_smoke.c') '-luser32' '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'clipboard full static-import suite build failed' }
$gateArgs = @((Join-Path $projectRoot 'tests/clipboard_check_pe98.py'))
if ($FixturePath -and -not $ProviderPath) {
    $defaultProvider = Join-Path $projectRoot 'build/clipboard/M98USER.DLL'
    if (Test-Path -LiteralPath $defaultProvider -PathType Leaf) {
        $ProviderPath = $defaultProvider
    }
}
if ($FixturePath) {
    if (-not (Test-Path -LiteralPath $FixturePath -PathType Leaf)) {
        throw "clipboard fixture missing: $FixturePath"
    }
    $gateArgs += (Resolve-Path -LiteralPath $FixturePath).Path
}
if ($ProviderPath) {
    if (-not (Test-Path -LiteralPath $ProviderPath -PathType Leaf)) {
        throw "clipboard provider missing: $ProviderPath"
    }
    if (-not $FixturePath) { throw 'provider PE gate requires a fixture path first' }
    $gateArgs += (Resolve-Path -LiteralPath $ProviderPath).Path
}
& python @gateArgs
if ($LASTEXITCODE -ne 0) { throw 'clipboard Win98 PE/import gate failed' }
if (-not $SkipHostExecution) {
    & $smoke '--require-api'
    if ($LASTEXITCODE -ne 0) { throw 'native host clipboard read-only oracle failed' }
    & $static
    if ($LASTEXITCODE -ne 0) { throw 'native host static clipboard import probe failed' }
    & $staticSuite '--require-api'
    if ($LASTEXITCODE -ne 0) { throw 'native host full static clipboard contract suite failed' }
    if ($FixturePath) {
        Copy-Item -LiteralPath $FixturePath -Destination (Join-Path $outDir 'CLIPFIX.DLL') -Force
        & $smoke '--bridge'
        if ($LASTEXITCODE -ne 0) { throw 'clipboard fixture contract smoke failed' }
    }
    if ($ProviderPath) {
        Copy-Item -LiteralPath $ProviderPath -Destination (Join-Path $outDir 'M98USER.DLL') -Force
        & $smoke '--shipping'
        if ($LASTEXITCODE -ne 0) { throw 'clipboard shipping provider contract smoke failed' }
    }
}
Write-Host "Guest handoff: $outDir"
Write-Host "Smoke SHA256: $((Get-FileHash -LiteralPath $smoke -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Host "Static SHA256: $((Get-FileHash -LiteralPath $static -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Host "Full static suite SHA256: $((Get-FileHash -LiteralPath $staticSuite -Algorithm SHA256).Hash.ToLowerInvariant())"
