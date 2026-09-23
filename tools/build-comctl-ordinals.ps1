# Build original COMCTL32 ordinal provider and a host behavior probe.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/comctl-ord'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$cc = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$windres = (Get-Command i686-w64-mingw32-windres -ErrorAction Stop).Source
$dlltool = (Get-Command i686-w64-mingw32-dlltool -ErrorAction Stop).Source
& python (Join-Path $root 'tests/make_comctl_icon_fixture.py') `
    (Join-Path $out 'comctl_icon_fixture.ico')
if ($LASTEXITCODE -ne 0) { throw 'COMCTL icon fixture generation failed' }
& $windres '-I' $out '-i' (Join-Path $root 'tests/comctl_icon_fixture.rc') `
    '-o' (Join-Path $out 'comctl_icon_fixture.o')
if ($LASTEXITCODE -ne 0) { throw 'COMCTL icon resource compilation failed' }
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections',
    '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--no-insert-timestamp')
& $cc @flags '-shared' '-Wl,--entry,_DllMain@12' '-Wl,--subsystem,windows:4.10' `
    '-o' (Join-Path $out 'M98CTL.DLL') `
    (Join-Path $root 'src/m98_comctl_ordinals.c') `
    (Join-Path $root 'src/m98ctl.c') '-lkernel32' '-luser32'
if ($LASTEXITCODE -ne 0) { throw 'COMCTL ordinal provider build failed' }
& $cc @flags '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' `
    '-o' (Join-Path $out 'comctl_ordinal_host.exe') `
    (Join-Path $root 'tests/comctl_ordinal_host.c') `
    (Join-Path $out 'comctl_icon_fixture.o') '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'COMCTL ordinal host probe build failed' }
& $dlltool '-d' (Join-Path $root 'tests/comctl_ordinal_imports.def') `
    '-l' (Join-Path $out 'libcomctl_ordinal.a') '-m' 'i386'
if ($LASTEXITCODE -ne 0) { throw 'COMCTL ordinal import library failed' }
& $cc @flags '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' `
    '-o' (Join-Path $out 'comctl_ordinal_import_probe.exe') `
    (Join-Path $root 'tests/comctl_ordinal_import_probe.c') `
    (Join-Path $out 'libcomctl_ordinal.a') '-lkernel32' '-luser32'
if ($LASTEXITCODE -ne 0) { throw 'COMCTL ordinal static probe build failed' }
& python (Join-Path $root 'tests/check_comctl_ordinal_pe98.py') `
    (Join-Path $out 'M98CTL.DLL') (Join-Path $out 'comctl_ordinal_host.exe') `
    (Join-Path $out 'comctl_ordinal_import_probe.exe')
if ($LASTEXITCODE -ne 0) { throw 'COMCTL ordinal PE gate failed' }
if (-not $SkipHostExecution) {
    & (Join-Path $out 'comctl_ordinal_host.exe')
    if ($LASTEXITCODE -ne 0) { throw 'COMCTL ordinal host behavior probe failed' }
}
Write-Host "COMCTL ordinal provider: $out"
