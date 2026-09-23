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
  '-Wl,--disable-nxcompat', '-Wl,--disable-tsaware'
)

& $compiler @flags '-o' (Join-Path $buildDir 'm98wrap.dll') `
  (Join-Path $projectRoot 'src/m98wrap.c') `
  (Join-Path $projectRoot 'src/wine_uppercase.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll build failed' }
& python (Join-Path $projectRoot 'tests/check_pe98.py') (Join-Path $buildDir 'm98wrap.dll')
if ($LASTEXITCODE -ne 0) { throw 'm98wrap.dll PE98 validation failed' }

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

Write-Host (Join-Path $buildDir 'm98wrap.dll')
Write-Host (Join-Path $buildDir 'dbghelp.dll')
Write-Host (Join-Path $buildDir 'dwmapi.dll')
Write-Host (Join-Path $buildDir 'bcrypt.dll')
