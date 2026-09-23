param([string]$OutputPath)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $OutputPath) { $OutputPath = Join-Path $projectRoot 'build/npp_seh_trace.exe' }
$outputDir = Split-Path -Parent $OutputPath
if ($outputDir) { New-Item -ItemType Directory -Path $outputDir -Force | Out-Null }
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @(
    '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror',
    '-ffreestanding', '-fno-builtin', '-nostdlib',
    '-Wl,--entry,_mainCRTStartup', '-Wl,--subsystem,console:4.10',
    '-Wl,--major-image-version,4', '-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase', '-Wl,--disable-nxcompat',
    '-Wl,--disable-tsaware'
)
& $compiler @flags '-o' $OutputPath (Join-Path $projectRoot 'tests/npp_seh_trace.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'npp_seh_trace compilation failed' }
& python (Join-Path $projectRoot 'tests/check_npp_seh_trace_pe98.py') $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'npp_seh_trace Windows 98 PE gate failed' }
Write-Host $OutputPath
