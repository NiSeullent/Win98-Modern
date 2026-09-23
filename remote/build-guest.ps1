param([string]$OutputPath)

$ErrorActionPreference = 'Stop'
$remoteRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $remoteRoot
$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
if (-not $OutputPath) { $OutputPath = Join-Path $buildDir 'm98agent.exe' }
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$flags = @(
    '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror',
    '-ffreestanding', '-fno-builtin', '-nostdlib',
    '-Wl,--entry,_mainCRTStartup', '-Wl,--subsystem,console:4.10',
    '-Wl,--major-image-version,4', '-Wl,--minor-image-version,10',
    '-Wl,--disable-dynamicbase', '-Wl,--disable-nxcompat',
    '-Wl,--disable-tsaware'
)
& $compiler @flags '-o' $OutputPath (Join-Path $remoteRoot 'guest/agent.c') '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'm98agent.exe compilation failed' }
& python (Join-Path $remoteRoot 'check_guest_pe.py') $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'm98agent.exe Windows 98 loader gate failed' }
Write-Host $OutputPath
