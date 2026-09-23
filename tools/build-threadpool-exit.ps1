# Isolated regression for DLL_PROCESS_DETACH after ExitProcess kills a lock owner.
param([switch]$SkipHostExecution)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'build/threadpool-exit'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir 'TPEXIT.EXE'
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source

$flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-nostdlib',
    '-Wl,--gc-sections','-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10','-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10','-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat','-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp')
& $compiler @flags '-o' $out `
    (Join-Path $projectRoot 'tests/threadpool_exit_smoke.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'threadpool exit regression build failed' }

& python (Join-Path $projectRoot 'tests/check_threadpool_exit_pe98.py') $out
if ($LASTEXITCODE -ne 0) { throw 'threadpool exit PE98 gate failed' }

if (-not $SkipHostExecution) {
    & $out
    if ($LASTEXITCODE -ne 0) { throw 'threadpool exit host regression failed' }
}
Write-Host "Threadpool exit guest handoff: $out"
