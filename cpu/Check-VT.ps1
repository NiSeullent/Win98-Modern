param(
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-i386.exe'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $QemuPath -PathType Leaf)) {
    throw "QEMU executable is missing: $QemuPath"
}

$accelerators = & $QemuPath -accel help 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "QEMU accelerator query failed (exit $LASTEXITCODE): $($accelerators -join ' ')"
}

$hasWhpx = $false
foreach ($line in $accelerators) {
    if ([string]$line -match '^\s*whpx(?:\s|$)') {
        $hasWhpx = $true
        break
    }
}
if (-not $hasWhpx) {
    throw "This QEMU build has no WHPX accelerator. Hardware virtualization is mandatory; TCG fallback is disabled. Available: $($accelerators -join ' ')"
}

Write-Output "WHPX backend is present in this QEMU build: $QemuPath"
Write-Output 'This checks the QEMU binary only. A real -accel whpx VM launch must still confirm host firmware, Windows feature and accelerator initialization.'
