param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/fls-integration'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin','-nostdlib',
  '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
  '-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,console:4.10',
  '-Wl,--major-image-version,4','-Wl,--minor-image-version,10',
  '-Wl,--disable-dynamicbase','-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
  '-Wl,--no-insert-timestamp')
foreach ($mode in @('direct','static','integrated')) {
  $modeFlags = @()
  if ($mode -eq 'static') { $modeFlags = @('-DM98_FLS_STATIC') }
  if ($mode -eq 'integrated') { $modeFlags = @('-DM98_FLS_INTEGRATED') }
  & $compiler @flags @modeFlags '-o' (Join-Path $outDir "fls_$mode.exe") `
    (Join-Path $projectRoot 'tests/fls_import_probe.c') '-lkernel32'
  if ($LASTEXITCODE -ne 0) { throw "FLS $mode probe build failed" }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'build/threadpool/TPMARK.DLL') -Destination $outDir -Force
Push-Location $projectRoot
try {
  & python 'tests/fls_integration_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'FLS static binding PE gate failed' }
  if (-not $SkipHostExecution) {
    & (Join-Path $outDir 'fls_static.exe')
    if ($LASTEXITCODE -ne 0) { throw 'FLS native static comparison failed' }
  }
} finally { Pop-Location }
