$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$output = Join-Path $root 'build/known_dll_switch.exe'
New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' $output (Join-Path $root 'remote/guest/known_dll_switch.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'KnownDLL mapping helper build failed' }
& python (Join-Path $root 'tests/check_known_dll_switch_pe98.py') $output
if ($LASTEXITCODE -ne 0) { throw 'KnownDLL mapping helper PE gate failed' }
Write-Host $output
