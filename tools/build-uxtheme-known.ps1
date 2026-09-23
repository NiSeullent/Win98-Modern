$ErrorActionPreference = 'Stop'

# KernelEx maps bare UXTHEME imports to its own KnownDLL. Build a single
# replacement from its pinned original implementation plus the six imports
# newly needed by the pinned Notepad++ image. No runtime LoadLibrary forwarder
# is used: KernelEx's original DLL deliberately rejects dynamic Win98 loads.
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$kernelEx = Join-Path $root 'third_party/KernelEx'
$pin = '31cdfc3560fc116637ee8ed7be31b12f3aacf5d1'
$actual = (& git -C $kernelEx rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $pin) {
    throw "KernelEx pin mismatch: expected $pin, got $actual"
}
$source = Join-Path $kernelEx 'auxiliary/uxtheme'
$output = Join-Path $root 'build/uxtheme-known'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$compiler = (Get-Command i686-w64-mingw32-gcc -ErrorAction Stop).Source
$common = @('-std=gnu11', '-Os', '-fno-builtin', '-ffunction-sections', '-fdata-sections')
$originalObject = Join-Path $output 'kernelex-uxtheme.o'
$metricObject = Join-Path $output 'kernelex-metric.o'
$newObject = Join-Path $output 'm98-uxtheme-additions.o'
$fontObject = Join-Path $output 'm98-uxtheme-sysfont.o'

# The pinned KernelEx source uses Microsoft's __asm block syntax. LLVM-MinGW
# accepts it with -fasm-blocks while preserving the upstream stack cleanup.
& $compiler @common '-fasm-blocks' '-c' (Join-Path $source 'uxtheme.c') '-o' $originalObject
if ($LASTEXITCODE -ne 0) { throw 'Original KernelEx UXTHEME compile failed' }
& $compiler @common '-Wno-inconsistent-dllimport' '-Wno-unknown-pragmas' '-c' (Join-Path $source 'metric.c') '-o' $metricObject
if ($LASTEXITCODE -ne 0) { throw 'Original KernelEx metric compile failed' }
# The new no-theme functions use separate m98_* names. Rename their DllMain
# so the original KernelEx DllMain remains the sole module entry point.
& $compiler @common '-DDllMain=m98UxThemeUnusedDllMain' '-c' (Join-Path $root 'src/uxtheme_shim.c') '-o' $newObject
if ($LASTEXITCODE -ne 0) { throw 'Modern UXTHEME additions compile failed' }
& $compiler @common '-Wall' '-Wextra' '-Werror' '-c' (Join-Path $root 'src/uxtheme_sysfont.c') '-o' $fontObject
if ($LASTEXITCODE -ne 0) { throw 'Win98 UXTHEME system font compile failed' }

$def = Join-Path $output 'uxtheme-known.def'
$originalDef = Get-Content (Join-Path $source 'uxtheme.def')
# LLD's module-definition parser expects a decimal BASE value. Preserve the
# pinned image base while converting the old MSVC-style hexadecimal literal.
$originalDef[0] = $originalDef[0] -replace 'BASE=0x7D030000', 'BASE=2097348608'
$replacedStubs = @(
    'CloseThemeData',
    'DrawThemeBackground',
    'DrawThemeParentBackground',
    'EnableThemeDialogTexture',
    'GetThemeBackgroundContentRect',
    'GetThemeColor',
    'GetThemeFont',
    'GetThemePartSize',
    'OpenThemeData',
    'SetWindowTheme'
)
# Preserve every upstream export name. Redirect only NPP's overlapping theme
# stubs to the Win98 no-theme behavior; leave useful upstream metrics intact.
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
$originalDef + $additions | Set-Content -LiteralPath $def -Encoding ascii
$dll = Join-Path $output 'UXTHEME.DLL'
& $compiler '-nostdlib' '-shared' '-Wl,--gc-sections' '-Wl,--no-insert-timestamp' '-Wl,--entry,_DllMain@12' '-Wl,--subsystem,windows:4.10' '-Wl,--major-image-version,4' '-Wl,--minor-image-version,10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' $dll $originalObject $metricObject $newObject $fontObject $def '-lkernel32' '-luser32' '-lgdi32'
if ($LASTEXITCODE -ne 0) { throw 'KnownDLL UXTHEME link failed' }
& python (Join-Path $root 'tests/check_uxtheme_known_pe98.py') $dll
if ($LASTEXITCODE -ne 0) { throw 'KnownDLL UXTHEME PE gate failed' }
$smoke = Join-Path $output 'uxtheme_known_host_smoke.exe'
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--no-insert-timestamp' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' $smoke (Join-Path $root 'tests/uxtheme_known_host_smoke.c') '-lkernel32' '-luser32'
if ($LASTEXITCODE -ne 0) { throw 'KnownDLL host smoke build failed' }
$fontSmoke = Join-Path $output 'uxtheme_sysfont_host_smoke.exe'
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-Wl,--no-insert-timestamp' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' $fontSmoke (Join-Path $root 'tests/uxtheme_sysfont_host_smoke.c') '-lkernel32' '-luser32'
if ($LASTEXITCODE -ne 0) { throw 'System-font host smoke build failed' }
$fontGuest = Join-Path $output 'uxtheme_sysfont_guest_probe.exe'
& $compiler '-std=c11' '-Os' '-Wall' '-Wextra' '-Werror' '-fno-builtin' '-nostdlib' '-DM98_GUEST_STATIC' '-Wl,--no-insert-timestamp' '-Wl,--entry,_mainCRTStartup' '-Wl,--subsystem,console:4.10' '-Wl,--disable-dynamicbase' '-Wl,--disable-nxcompat' '-Wl,--disable-tsaware' '-o' $fontGuest (Join-Path $root 'tests/uxtheme_sysfont_host_smoke.c') '-luxtheme' '-lkernel32' '-luser32'
if ($LASTEXITCODE -ne 0) { throw 'System-font guest probe build failed' }
& python (Join-Path $root 'tests/check_uxtheme_sysfont_guest_pe98.py') $fontGuest
if ($LASTEXITCODE -ne 0) { throw 'System-font guest probe PE gate failed' }
if ([System.Environment]::OSVersion.Platform -eq 'Win32NT') {
    Push-Location $root
    try {
        & $smoke
        if ($LASTEXITCODE -ne 0) { throw 'KnownDLL host smoke failed' }
        & $fontSmoke
        if ($LASTEXITCODE -ne 0) { throw 'System-font host smoke failed' }
    } finally { Pop-Location }
}
Write-Host $dll
