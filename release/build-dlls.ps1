$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path -LiteralPath (Join-Path $scriptDir 'src') -PathType Container) {
  $projectRoot = $scriptDir  # Extracted release ZIP.
} else {
  $projectRoot = Split-Path -Parent $scriptDir  # Source checkout.
}
$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @(
  '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
  '-ffunction-sections', '-fdata-sections', '-nostdlib', '-shared',
  '-Wl,--gc-sections', '-Wl,--entry,_DllMain@12',
  '-Wl,--subsystem,windows:4.10', '-Wl,--major-image-version,4',
  '-Wl,--minor-image-version,10', '-Wl,--disable-dynamicbase',
  '-Wl,--disable-nxcompat', '-Wl,--disable-tsaware',
  '-Wl,--no-insert-timestamp'
)

$wrapperSources = @(& (Join-Path $projectRoot 'tools/m98wrap-sources.ps1') -ProjectRoot $projectRoot)
& $compiler @flags '-o' (Join-Path $buildDir 'm98wrap.dll') @wrapperSources '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_pe98.py') (Join-Path $buildDir 'm98wrap.dll')
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll PE98 validation failed' }

# Rebuild the focused KERNEL32 ports with their direct/static probes and
# the current API-table smoke. The isolated builds must be byte-identical to
# the DLL this release actually ships, so their PE gates cover that artifact.
& (Join-Path $projectRoot 'tools/build-finalpath.ps1')
& (Join-Path $projectRoot 'tools/build-stream.ps1')
& (Join-Path $projectRoot 'tools/build-localeinfo.ps1')
& (Join-Path $projectRoot 'tools/build-restart.ps1')
& (Join-Path $projectRoot 'tools/build-processpath.ps1')
& (Join-Path $projectRoot 'tools/build-condition.ps1')
& (Join-Path $projectRoot 'tools/build-nls-ex.ps1')
& (Join-Path $projectRoot 'tools/build-threadpool.ps1')
& (Join-Path $projectRoot 'tools/build-threadpool-exit.ps1')
& (Join-Path $projectRoot 'tools/build-initonce.ps1')
& (Join-Path $projectRoot 'tools/build-slist.ps1')
& (Join-Path $projectRoot 'tools/build-fls.ps1')
& (Join-Path $projectRoot 'tools/build-fls-rundown-race.ps1')
& (Join-Path $projectRoot 'tools/build-fls-integration.ps1')
& (Join-Path $projectRoot 'tools/build-thread-lifecycle.ps1')
& (Join-Path $projectRoot 'tools/build-clipboard.ps1')
Copy-Item -LiteralPath (Join-Path $buildDir 'clipboard/M98USER.DLL') `
  -Destination (Join-Path $buildDir 'M98USER.DLL') -Force
# Put the exact shipping wrapper beside each integration probe. The executable
# directory precedes the working directory in the loader search order, so a
# leftover local DLL must not silently substitute for the artifact being tested.
foreach ($testDir in @('initonce', 'threadpool', 'slist', 'fls-integration')) {
  Copy-Item -LiteralPath (Join-Path $buildDir 'm98wrap.dll') `
    -Destination (Join-Path $buildDir "$testDir/m98wrap.dll") -Force
}
Push-Location $buildDir
try {
  & (Join-Path $buildDir 'fls-integration/fls_integrated.exe')
  if ($LASTEXITCODE -ne 0) { throw 'Shipping m98wrap.dll FLS integration failed' }
  & (Join-Path $buildDir 'initonce/initonce_integrated_probe.exe')
  if ($LASTEXITCODE -ne 0) { throw 'Shipping m98wrap.dll InitOnce integration failed' }
  & (Join-Path $buildDir 'slist/slist_integrated_probe.exe')
  if ($LASTEXITCODE -ne 0) { throw 'Shipping m98wrap.dll SList integration failed' }
  & (Join-Path $buildDir 'threadpool/threadpool_callback_integrated.exe')
  if ($LASTEXITCODE -ne 0) { throw 'Shipping m98wrap.dll callback integration failed' }
} finally { Pop-Location }
$wrapperHash = (Get-FileHash -LiteralPath (Join-Path $buildDir 'm98wrap.dll') -Algorithm SHA256).Hash
foreach ($candidate in @('finalpath/m98wrap.dll', 'stream/m98wrap.dll',
                         'localeinfo/m98wrap.dll', 'restart/m98wrap.dll',
                         'processpath/m98wrap.dll', 'condition/m98wrap.dll',
                         'initonce/m98wrap.dll', 'threadpool/m98wrap.dll',
                         'slist/m98wrap.dll', 'fls-integration/m98wrap.dll')) {
  $actual = (Get-FileHash -LiteralPath (Join-Path $buildDir $candidate) -Algorithm SHA256).Hash
  if ($actual -ne $wrapperHash) {
    throw "KERNEL32 test DLL differs from release m98wrap.dll: $candidate"
  }
}

& $compiler @flags '-o' (Join-Path $buildDir 'm98shell.dll') `
  (Join-Path $projectRoot 'src/m98shell.c') (Join-Path $projectRoot 'src/m98shell_openfolder.c') '-lshell32' '-lole32' '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98shell.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_m98shell_pe98.py') (Join-Path $buildDir 'm98shell.dll')
if ($LASTEXITCODE -ne 0) { throw 'm98shell.dll PE98 validation failed' }

& $compiler @flags '-o' (Join-Path $buildDir 'dbghelp.dll') `
  (Join-Path $projectRoot 'src/dbghelp_shim.c') `
  (Join-Path $projectRoot 'src/dbghelp_shim.def') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'dbghelp.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_dbghelp_pe98.py') (Join-Path $buildDir 'dbghelp.dll')
if ($LASTEXITCODE -ne 0) { throw 'dbghelp.dll PE98 validation failed' }

& $compiler @flags '-o' (Join-Path $buildDir 'dwmapi.dll') `
  (Join-Path $projectRoot 'src/dwmapi_shim.c') `
  (Join-Path $projectRoot 'src/dwmapi_shim.def')
if ($LASTEXITCODE -ne 0) { throw 'dwmapi.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_dwmapi_pe98.py') (Join-Path $buildDir 'dwmapi.dll')
if ($LASTEXITCODE -ne 0) { throw 'dwmapi.dll PE98 validation failed' }

& $compiler @flags '-o' (Join-Path $buildDir 'bcrypt.dll') `
  (Join-Path $projectRoot 'src/bcrypt_shim.c') `
  (Join-Path $projectRoot 'src/bcrypt_shim.def') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'bcrypt.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_bcrypt_pe98.py') (Join-Path $buildDir 'bcrypt.dll')
if ($LASTEXITCODE -ne 0) { throw 'bcrypt.dll PE98 validation failed' }

# The installed KernelEx contents token is "m98adv", which fits DOS 8.3.
# Preserve the guest-tested long-name link output and copy it under that
# installation name; the PE export table exposes only get_api_table.
$advDir = Join-Path $buildDir 'advapi'
New-Item -ItemType Directory -Path $advDir -Force | Out-Null
$advDll = Join-Path $advDir 'm98advapi.dll'
$testFlags = @(
  '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
  '-nostdlib', '-Wl,--entry,_mainCRTStartup',
  '-Wl,--subsystem,console:4.10', '-Wl,--major-image-version,4',
  '-Wl,--minor-image-version,10', '-Wl,--disable-dynamicbase',
  '-Wl,--disable-nxcompat', '-Wl,--disable-tsaware',
  '-Wl,--no-insert-timestamp'
)
& $compiler @flags '-o' $advDll (Join-Path $projectRoot 'src/m98advapi.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'm98advapi.dll build failed' }
$advSmoke = Join-Path $advDir 'advapi_smoke.exe'
$advImport = Join-Path $advDir 'advapi_import_probe.exe'
& $compiler @testFlags '-o' $advSmoke (Join-Path $projectRoot 'tests/advapi_smoke.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'ADVAPI direct smoke build failed' }
& $compiler @testFlags '-o' $advImport (Join-Path $projectRoot 'tests/advapi_import_probe.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'ADVAPI static-import probe build failed' }
& python (Join-Path $projectRoot 'tests/check_m98advapi_pe98.py') $advDll $advImport $advSmoke
if ($LASTEXITCODE -ne 0) { throw 'm98advapi.dll PE98 validation failed' }
Copy-Item -LiteralPath $advDll -Destination (Join-Path $buildDir 'm98adv.dll') -Force

# Only three exact, hashed KernelEx 4.5.2 source files are redistributed.
# Hash checks work in an extracted ZIP without a git checkout or submodule.
$knownSource = Join-Path $projectRoot 'third_party/KernelEx/auxiliary/uxtheme'
$pinned = [ordered]@{
  'uxtheme.c' = '1DF5AE49D5545F756CCA950842809443ADA4B13DD980AC6A67EF686737BE1020'
  'metric.c' = 'DC6718B383230FC948907AB95D5376E476727B5C29639DF29DCFF59EE39F5EC6'
  'uxtheme.def' = 'BF43B22625E5EE24C3727FD6B0D1BF51116705ABFBB197F95DB962889D0B446F'
}
foreach ($name in $pinned.Keys) {
  $sourcePath = Join-Path $knownSource $name
  $item = Get-Item -LiteralPath $sourcePath -ErrorAction Stop
  if ($item.PSIsContainer -or $item.Length -eq 0 -or
      ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
    throw "KernelEx source is missing, empty or linked: $name"
  }
  $actual = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
  if ($actual -ne $pinned[$name]) {
    throw "KernelEx source hash mismatch: $name (expected $($pinned[$name]), got $actual)"
  }
}
$uxDir = Join-Path $buildDir 'uxtheme-known'
New-Item -ItemType Directory -Path $uxDir -Force | Out-Null
$common = @('-std=gnu11', '-Os', '-fno-builtin', '-ffunction-sections', '-fdata-sections')
$originalObject = Join-Path $uxDir 'kernelex-uxtheme.o'
$metricObject = Join-Path $uxDir 'kernelex-metric.o'
$newObject = Join-Path $uxDir 'm98-uxtheme-additions.o'
$fontObject = Join-Path $uxDir 'm98-uxtheme-sysfont.o'
& $compiler @common '-fasm-blocks' '-c' (Join-Path $knownSource 'uxtheme.c') '-o' $originalObject
if ($LASTEXITCODE -ne 0) { throw 'Pinned KernelEx UXTHEME compile failed' }
& $compiler @common '-Wno-inconsistent-dllimport' '-Wno-unknown-pragmas' '-c' (Join-Path $knownSource 'metric.c') '-o' $metricObject
if ($LASTEXITCODE -ne 0) { throw 'Pinned KernelEx theme metric compile failed' }
& $compiler @common '-DDllMain=m98UxThemeUnusedDllMain' '-c' (Join-Path $projectRoot 'src/uxtheme_shim.c') '-o' $newObject
if ($LASTEXITCODE -ne 0) { throw 'No-theme additions compile failed' }
& $compiler @common '-Wall' '-Wextra' '-Werror' '-c' (Join-Path $projectRoot 'src/uxtheme_sysfont.c') '-o' $fontObject
if ($LASTEXITCODE -ne 0) { throw 'Win98 system-font replacement compile failed' }

$originalDef = Get-Content -LiteralPath (Join-Path $knownSource 'uxtheme.def') -Encoding ascii
$originalDef[0] = $originalDef[0] -replace 'BASE=0x7D030000', 'BASE=2097348608'
$replacedStubs = @(
  'CloseThemeData', 'DrawThemeBackground', 'DrawThemeParentBackground',
  'EnableThemeDialogTexture', 'GetThemeBackgroundContentRect',
  'GetThemeColor', 'GetThemeFont', 'GetThemePartSize',
  'OpenThemeData', 'SetWindowTheme'
)
$originalDef = @($originalDef | ForEach-Object {
  $name = $_.Trim()
  if ($replacedStubs -contains $name) { "${name}=m98_${name}" }
  elseif ($name -eq 'GetThemeSysFont') { 'GetThemeSysFont=m98_GetThemeSysFont' }
  else { $_ }
})
$additions = @(
  'BeginBufferedAnimation=m98_BeginBufferedAnimation',
  'BufferedPaintRenderAnimation=m98_BufferedPaintRenderAnimation',
  'BufferedPaintStopAllAnimations=m98_BufferedPaintStopAllAnimations',
  'DrawThemeTextEx=m98_DrawThemeTextEx',
  'EndBufferedAnimation=m98_EndBufferedAnimation',
  'GetThemeTransitionDuration=m98_GetThemeTransitionDuration'
)
$uxDef = Join-Path $uxDir 'uxtheme-known.def'
$originalDef + $additions | Set-Content -LiteralPath $uxDef -Encoding ascii
$uxDll = Join-Path $uxDir 'UXTHEME.DLL'
& $compiler '-nostdlib' '-shared' '-Wl,--gc-sections' '-Wl,--no-insert-timestamp' '-Wl,--entry,_DllMain@12' '-Wl,--subsystem,windows:4.10' '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' $uxDll $originalObject $metricObject $newObject $fontObject $uxDef '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'KernelEx UXTHEME KnownDLL build failed' }
$uxGate = Join-Path $projectRoot 'tests/check_uxtheme_release_pe98.py'
if (-not (Test-Path -LiteralPath $uxGate -PathType Leaf)) {
  $uxGate = Join-Path $scriptDir 'check-uxtheme-release-pe98.py'
}
& python $uxGate $uxDll
if ($LASTEXITCODE -ne 0) { throw 'KernelEx UXTHEME redistributable PE gate failed' }

$switch = Join-Path $buildDir 'known_dll_switch.exe'
& $compiler @testFlags '-o' $switch (Join-Path $projectRoot 'remote/guest/known_dll_switch.c') '-lkernel32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'Guarded KnownDLL switch build failed' }
& python (Join-Path $projectRoot 'tests/check_known_dll_switch_pe98.py') $switch
if ($LASTEXITCODE -ne 0) { throw 'Guarded KnownDLL switch PE gate failed' }

Write-Host (Join-Path $buildDir 'm98wrap.dll')
Write-Host (Join-Path $buildDir 'm98shell.dll')
Write-Host (Join-Path $buildDir 'dbghelp.dll')
Write-Host (Join-Path $buildDir 'dwmapi.dll')
Write-Host (Join-Path $buildDir 'bcrypt.dll')
Write-Host (Join-Path $buildDir 'm98adv.dll')
Write-Host $uxDll
Write-Host $switch
