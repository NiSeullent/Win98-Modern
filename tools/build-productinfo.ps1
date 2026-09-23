# Isolated GetProductInfo build; guest tests require KernelEx and are separate.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/productinfo'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$cc = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$dllFlags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-shared',
    '-Wl,--gc-sections','-Wl,--entry,_DllMain@12',
    '-Wl,--subsystem,windows:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware')
$exeFlags = @('-std=c11','-Os','-Wall','-Wextra','-Werror',
    '-fno-builtin','-nostdlib','-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware')
$dllSources = @(& (Join-Path $PSScriptRoot 'm98wrap-sources.ps1') -ProjectRoot $root)
& $cc @dllFlags '-o' (Join-Path $out 'm98wrap.dll') @dllSources '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98wrap build failed' }
& $cc @exeFlags '-o' (Join-Path $out 'productinfo_smoke.exe') (Join-Path $root 'tests/productinfo_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'direct smoke build failed' }
& $cc @exeFlags '-o' (Join-Path $out 'productinfo_import_probe.exe') (Join-Path $root 'tests/productinfo_import_probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'import probe build failed' }
Push-Location $root
try {
    & python 'tests/check_productinfo_pe98.py'
    if ($LASTEXITCODE -ne 0) { throw 'static gate failed' }
    & (Join-Path $out 'productinfo_smoke.exe')
    if ($LASTEXITCODE -ne 0) { throw 'host direct smoke failed' }
    & (Join-Path $out 'productinfo_import_probe.exe')
    if ($LASTEXITCODE -ne 0) { throw 'host static import smoke failed' }
} finally { Pop-Location }
