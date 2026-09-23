# Build experimental PNG-capable COMCTL32 ordinal provider without touching v1.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/comctl-png'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$cc = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$windres = (Get-Command i686-w64-mingw32-windres -ErrorAction Stop).Source
& python (Join-Path $root 'tests/make_comctl_png_fixture.py') `
    (Join-Path $out 'comctl_png_fixture.ico')
if ($LASTEXITCODE -ne 0) { throw 'PNG icon fixture generation failed' }
& $windres '-I' $out '-i' (Join-Path $root 'tests/comctl_png_fixture.rc') `
    '-o' (Join-Path $out 'comctl_png_fixture.o')
if ($LASTEXITCODE -ne 0) { throw 'PNG icon resource compilation failed' }
& python (Join-Path $root 'tests/make_comctl_depth_fixture.py') `
    (Join-Path $out 'comctl_png_depth_fixture.ico')
if ($LASTEXITCODE -ne 0) { throw 'depth icon fixture generation failed' }
& $windres '-I' $out '-i' (Join-Path $root 'tests/comctl_png_depth_fixture.rc') `
    '-o' (Join-Path $out 'comctl_png_depth_fixture.o')
if ($LASTEXITCODE -ne 0) { throw 'depth icon resource compilation failed' }
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--no-insert-timestamp')
$decoderFlags = @('-DM98_WITH_PNG','-DLODEPNG_NO_COMPILE_ENCODER',
    '-DLODEPNG_NO_COMPILE_DISK','-DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS',
    '-DLODEPNG_NO_COMPILE_ERROR_TEXT','-DLODEPNG_NO_COMPILE_ALLOCATORS',
    '-DLODEPNG_MAX_ALLOC=8388608')
& $cc @flags @decoderFlags '-shared' '-Wl,--entry,_DllMain@12' `
    '-Wl,--subsystem,windows:4.10' '-o' (Join-Path $out 'M98CTLP.DLL') `
    (Join-Path $root 'src/m98_comctl_ordinals.c') `
    (Join-Path $root 'src/m98_icon_choice.c') `
    (Join-Path $root 'src/m98_icon_png.c') `
    (Join-Path $root 'src/vendor/lodepng/lodepng.c') `
    (Join-Path $root 'src/m98ctl.c') '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'PNG provider build failed' }
& $cc @flags '-Wl,--entry,_mainCRTStartup' `
    '-Wl,--subsystem,console:4.10' `
    '-o' (Join-Path $out 'comctl_png_host.exe') `
    (Join-Path $root 'tests/comctl_png_host.c') `
    (Join-Path $out 'comctl_png_fixture.o') `
    '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'PNG host probe build failed' }
& $cc @flags '-Wl,--entry,_mainCRTStartup' `
    '-Wl,--subsystem,console:4.10' `
    '-o' (Join-Path $out 'comctl_png_depth_host.exe') `
    (Join-Path $root 'tests/comctl_png_depth_host.c') `
    (Join-Path $out 'comctl_png_depth_fixture.o') `
    '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'depth host probe build failed' }
& $cc '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' `
    '-o' (Join-Path $out 'comctl_icon_choice_contract.exe') `
    (Join-Path $root 'tests/comctl_icon_choice_contract.c') `
    (Join-Path $root 'src/m98_icon_choice.c')
if ($LASTEXITCODE -ne 0) { throw 'icon choice contract build failed' }
& python (Join-Path $root 'tests/check_comctl_ordinal_pe98.py') `
    (Join-Path $out 'M98CTLP.DLL') (Join-Path $out 'comctl_png_host.exe') `
    (Join-Path $out 'comctl_png_depth_host.exe')
if ($LASTEXITCODE -ne 0) { throw 'PNG provider PE gate failed' }
if (-not $SkipHostExecution) {
    & (Join-Path $out 'comctl_png_host.exe')
    if ($LASTEXITCODE -ne 0) { throw 'PNG host behavior probe failed' }
    & (Join-Path $out 'comctl_png_depth_host.exe')
    if ($LASTEXITCODE -ne 0) { throw 'PNG display-depth probe failed' }
    & (Join-Path $out 'comctl_icon_choice_contract.exe')
    if ($LASTEXITCODE -ne 0) { throw 'icon choice contract probe failed' }
}
Write-Host "COMCTL PNG provider: $out"
