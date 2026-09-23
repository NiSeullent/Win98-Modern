$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin','-nostdlib',
  '-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,console:4.10',
  '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat','-Wl,--disable-tsaware')
$output = Join-Path $buildDir 'remote_fixture.exe'
& $compiler @flags '-o' $output (Join-Path $PSScriptRoot 'tests/fixture.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'Remote fixture build failed.' }
& python (Join-Path $PSScriptRoot 'check_guest_pe.py') $output
if ($LASTEXITCODE -ne 0) { throw 'Remote fixture native Win98 PE check failed.' }
