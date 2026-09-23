# Build one independent GDI32 KernelEx provider and its direct-call fixture.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/gdi-alpha'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$cc = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--disable-tsaware','-Wl,--no-insert-timestamp',
    '-shared','-Wl,--entry,_DllMain@12','-Wl,--subsystem,windows:4.10')
foreach ($entry in @(
    @{ Name='M98GDI.DLL'; Table='src/m98gdi.c' },
    @{ Name='GDIFIX.DLL'; Table='tests/gdi_alpha_fixture.c' }
)) {
    & $cc @flags '-o' (Join-Path $out $entry.Name) `
        (Join-Path $root 'src/m98_gdi_alpha.c') `
        (Join-Path $root $entry.Table) '-lkernel32'
    if ($LASTEXITCODE -ne 0) { throw "GDI provider build failed: $($entry.Name)" }
}
& $cc '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' `
    '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' `
    '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' `
    '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' `
    '-Wl,--no-insert-timestamp' '-o' (Join-Path $out 'gdi_alpha_host.exe') `
    (Join-Path $root 'tests/gdi_alpha_host.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'GDI host probe build failed' }
& $cc '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' `
    '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' `
    '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' `
    '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' `
    '-Wl,--no-insert-timestamp' '-o' (Join-Path $out 'gdi_alpha_import_probe.exe') `
    (Join-Path $root 'tests/gdi_alpha_import_probe.c') '-lgdi32' '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'GDI static import probe build failed' }
& $cc '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' `
    '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' `
    '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' `
    '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' `
    '-Wl,--no-insert-timestamp' '-o' (Join-Path $out 'gdi_alpha_lifetime.exe') `
    (Join-Path $root 'tests/gdi_alpha_lifetime.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'GDI provider lifetime probe build failed' }
foreach ($contract in @(
    @{ Name='gdi_alpha_direct_contract.exe'; Extra=@() },
    @{ Name='gdi_alpha_static_contract.exe'; Extra=@('-DM98_STATIC_GDI_IMPORTS') }
)) {
    & $cc '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' `
        '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' `
        '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' `
        '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' `
        '-Wl,--no-insert-timestamp' @($contract.Extra) '-o' (Join-Path $out $contract.Name) `
        (Join-Path $root 'tests/gdi_alpha_contract.c') '-lgdi32' '-lkernel32'
    if ($LASTEXITCODE -ne 0) { throw "GDI guest contract build failed: $($contract.Name)" }
}
& python (Join-Path $root 'tests/gdi_alpha_check_pe98.py') `
    (Join-Path $out 'M98GDI.DLL') (Join-Path $out 'GDIFIX.DLL') `
    (Join-Path $out 'gdi_alpha_host.exe') `
    (Join-Path $out 'gdi_alpha_import_probe.exe') `
    (Join-Path $out 'gdi_alpha_lifetime.exe') `
    (Join-Path $out 'gdi_alpha_direct_contract.exe') `
    (Join-Path $out 'gdi_alpha_static_contract.exe')
if ($LASTEXITCODE -ne 0) { throw 'GDI PE/import gate failed' }
if (-not $SkipHostExecution) {
    & (Join-Path $out 'gdi_alpha_host.exe')
    if ($LASTEXITCODE -ne 0) { throw 'GDI binary checks failed' }
    & (Join-Path $out 'gdi_alpha_lifetime.exe')
    if ($LASTEXITCODE -ne 0) { throw 'GDI provider lifetime checks failed' }
}
Write-Host "GDI provider and fixture: $out"
