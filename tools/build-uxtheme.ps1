$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-nostdlib','-shared','-Wl,--gc-sections','-Wl,--entry,_DllMain@12','-Wl,--subsystem,windows:4.10','-Wl,--major-image-version,4','-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat','-Wl,--disable-tsaware')
$dll = Join-Path $buildDir 'm98uxtheme.dll'
& $compiler @flags '-o' $dll (Join-Path $projectRoot 'src/uxtheme_shim.c') (Join-Path $projectRoot 'src/uxtheme_shim.def') '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'UXTHEME bridge build failed' }
& python (Join-Path $projectRoot 'tests/check_uxtheme_pe98.py') $dll
if ($LASTEXITCODE -ne 0) { throw 'UXTHEME bridge Win98 PE gate failed' }
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'uxtheme_smoke.exe') (Join-Path $projectRoot 'tests/uxtheme_smoke.c') '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'UXTHEME smoke build failed' }
Write-Host $dll
