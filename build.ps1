param([switch]$Release)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-nostdlib','-shared','-Wl,--gc-sections','-Wl,--entry,_DllMain@12','-Wl,--subsystem,windows:4.10','-Wl,--major-image-version,4','-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat','-Wl,--disable-tsaware')
& $compiler @flags '-o' (Join-Path $buildDir 'm98wrap.dll') (Join-Path $projectRoot 'src/m98wrap.c') (Join-Path $projectRoot 'src/wine_uppercase.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_pe98.py') (Join-Path $buildDir 'm98wrap.dll')
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll Windows 98 loader check failed' }
& $compiler @flags '-o' (Join-Path $buildDir 'dbghelp.dll') (Join-Path $projectRoot 'src/dbghelp_shim.c') (Join-Path $projectRoot 'src/dbghelp_shim.def') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'dbghelp.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_dbghelp_pe98.py') (Join-Path $buildDir 'dbghelp.dll')
if ($LASTEXITCODE -ne 0) { throw 'dbghelp.dll Windows 98 loader check failed' }
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'dbghelp_smoke.exe') (Join-Path $projectRoot 'tests/dbghelp_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'dbghelp_smoke.exe build failed' }
& $compiler @flags '-o' (Join-Path $buildDir 'dwmapi.dll') (Join-Path $projectRoot 'src/dwmapi_shim.c') (Join-Path $projectRoot 'src/dwmapi_shim.def')
if ($LASTEXITCODE -ne 0) { throw 'dwmapi.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_dwmapi_pe98.py') (Join-Path $buildDir 'dwmapi.dll')
if ($LASTEXITCODE -ne 0) { throw 'dwmapi.dll Windows 98 loader check failed' }
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'dwmapi_smoke.exe') (Join-Path $projectRoot 'tests/dwmapi_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'dwmapi_smoke.exe build failed' }
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'smoke.exe') (Join-Path $projectRoot 'tests/smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'smoke.exe build failed' }
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'memprobe.exe') (Join-Path $projectRoot 'memory/probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'memprobe.exe build failed' }
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'memstress.exe') (Join-Path $projectRoot 'memory/stress.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'memstress.exe build failed' }
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'import_probe.exe') (Join-Path $projectRoot 'tests/import_probe.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'import_probe.exe build failed' }
& $compiler '-std=c89' '-Os' '-Wall' '-Wextra' '-Werror' '-ffreestanding' '-fno-builtin' '-nostdlib' '-Wl,-e,_cpuapp_entry@0' '-Wl,--subsystem,console:4.10' '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' (Join-Path $buildDir 'cpuapp.exe') (Join-Path $projectRoot 'cpu/cpuapp.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'cpuapp.exe build failed' }
Copy-Item -LiteralPath (Join-Path $projectRoot 'cpu/app-profiles.ini') -Destination (Join-Path $buildDir 'app-profiles.ini') -Force
Write-Host (Join-Path $buildDir 'm98wrap.dll')
