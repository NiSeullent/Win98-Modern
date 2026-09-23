# Standalone USER32 clipboard provider and identical direct-call fixture.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/clipboard'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--disable-tsaware','-Wl,--no-insert-timestamp',
    '-shared','-Wl,--entry,_DllMain@12','-Wl,--subsystem,windows:4.10')
foreach ($variant in @(
    @{ Name='M98USER.DLL'; Table='src/m98user.c' },
    @{ Name='CLIPFIX.DLL'; Table='tests/clipboard_fixture.c' }
)) {
    & $compiler @flags '-o' (Join-Path $outDir $variant.Name) `
        (Join-Path $projectRoot 'src/m98_clipboard.c') `
        (Join-Path $projectRoot $variant.Table) '-luser32' '-lkernel32'
    if ($LASTEXITCODE -ne 0) { throw "Clipboard build failed: $($variant.Name)" }
}
$testBuilder = Join-Path $projectRoot 'tools/build-clipboard-tests.ps1'
if (Test-Path -LiteralPath $testBuilder) {
    # The separate test owner controls PE/import gates and native/bridge probes.
    # Guest mutation is deliberately never enabled by this builder.
    if ($SkipHostExecution) {
        & $testBuilder -FixturePath (Join-Path $outDir 'CLIPFIX.DLL') -SkipHostExecution
    } else {
        & $testBuilder -FixturePath (Join-Path $outDir 'CLIPFIX.DLL')
    }
    if ($LASTEXITCODE -ne 0) { throw 'Clipboard independent test build/gate failed' }
}
Write-Host "Clipboard provider and fixture: $outDir"
