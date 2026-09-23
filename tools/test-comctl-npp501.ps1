# Probe the pinned app's actual icon 501 without shipping its resource.
param([Parameter(Mandatory=$true)][string]$NotepadExe)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/comctl-png'
if (-not (Test-Path (Join-Path $out 'M98CTLP.DLL'))) {
    throw 'Build the frozen PNG provider first'
}
$expected = '960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78'
$actual = (Get-FileHash $NotepadExe -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw 'Notepad++ 8.9.8 executable hash mismatch' }
& python (Join-Path $root 'tests/extract_group_icon.py') `
    $NotepadExe 501 (Join-Path $out 'npp501.ico')
if ($LASTEXITCODE -ne 0) { throw 'Notepad++ icon extraction failed' }
$windres = (Get-Command i686-w64-mingw32-windres -ErrorAction Stop).Source
$cc = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
& $windres '-I' $out '-i' (Join-Path $root 'tests/comctl_png_npp501.rc') `
    '-o' (Join-Path $out 'npp501.o')
if ($LASTEXITCODE -ne 0) { throw 'Notepad++ icon resource compilation failed' }
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-nostdlib','-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat',
    '-Wl,--no-insert-timestamp')
& $cc @flags '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' `
    '-o' (Join-Path $out 'comctl_png_npp501_host.exe') `
    (Join-Path $root 'tests/comctl_png_npp501_host.c') `
    (Join-Path $out 'npp501.o') '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'Notepad++ icon host build failed' }
& python (Join-Path $root 'tests/check_comctl_ordinal_pe98.py') `
    (Join-Path $out 'comctl_png_npp501_host.exe')
if ($LASTEXITCODE -ne 0) { throw 'Notepad++ icon PE gate failed' }
& (Join-Path $out 'comctl_png_npp501_host.exe')
if ($LASTEXITCODE -ne 0) { throw 'Notepad++ icon host probe failed' }
