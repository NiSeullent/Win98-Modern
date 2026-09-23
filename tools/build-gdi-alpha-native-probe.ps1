# Build the read-only, offscreen MSIMG32 identity and pixel baseline probe.
param([string]$OutputPath = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $projectRoot 'build/gdi-alpha-audit/GDI_NATIVE.EXE' }
$outDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$compileFlags = @(
    '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
    '-nostdlib', '-Wl,--entry,_mainCRTStartup',
    '-Wl,--subsystem,console:4.10', '-Wl,--major-image-version,4',
    '-Wl,--minor-image-version,10', '-Wl,--disable-dynamicbase',
    '-Wl,--disable-nxcompat', '-Wl,--disable-tsaware',
    '-Wl,--no-insert-timestamp'
)
& $compiler @compileFlags '-o' $OutputPath (Join-Path $projectRoot 'tests/gdi_alpha_native_probe.c') '-lgdi32' '-lkernel32'
if ($LASTEXITCODE -ne 0) { throw 'native MSIMG32 probe build failed' }
& python (Join-Path $projectRoot 'tests/gdi_alpha_native_check_pe98.py') $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'native MSIMG32 probe PE gate failed' }
Write-Host "Native GDI probe: $OutputPath"
Write-Host "SHA-256: $((Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant())"
