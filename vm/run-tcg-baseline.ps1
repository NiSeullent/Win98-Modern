# Installation/testing baseline only: QEMU TCG is software emulation and does not
# satisfy the project's mandatory VT-x/AMD-V accelerated execution requirement.
param(
  [switch]$Install,
  [switch]$Share,
  [string]$DiskPath,
  [int]$MemoryMiB = 256
)

$ErrorActionPreference = 'Stop'
$vmDirectory = $PSScriptRoot
$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'
$disk = if ($DiskPath) { $DiskPath } else { Join-Path $vmDirectory 'win98se-ko.qcow2' }
$iso = Join-Path $vmDirectory 'Win98-SE-ko-OEM.iso'
$shareDirectory = Join-Path $vmDirectory 'share'

if (-not (Test-Path -LiteralPath $qemu)) { throw "QEMU is missing: $qemu" }
if (-not (Test-Path -LiteralPath $disk)) { throw "VM disk is missing: $disk" }
if (-not (Test-Path -LiteralPath $iso)) { throw "Installation ISO is missing: $iso" }
if ($Share -and -not (Test-Path -LiteralPath $shareDirectory)) { throw "Share is missing: $shareDirectory" }
if ($MemoryMiB -lt 16) { throw 'MemoryMiB must be at least 16.' }

$bootOrder = if ($Install) { 'dc' } else { 'c' }
$vmArgs = @(
  '-accel', 'tcg',
  '-machine', 'pc-i440fx-9.2,acpi=off',
  '-cpu', 'pentium',
  '-smp', '1',
  '-m', "$MemoryMiB",
  '-vga', 'cirrus',
  '-drive', "file=$disk,format=qcow2,if=ide,index=0,media=disk",
  '-drive', "file=$iso,format=raw,if=ide,index=2,media=cdrom,readonly=on",
  '-boot', "order=$bootOrder",
  '-display', 'none',
  '-vnc', '127.0.0.1:11',
  '-monitor', 'tcp:127.0.0.1:4545,server,nowait',
  '-net', 'none',
  '-rtc', 'base=localtime',
  '-name', 'Win98SE-Korean-Modern'
)
if ($Share) {
  $vmArgs += @('-fda', "fat:floppy:$($shareDirectory.Replace('\', '/'))")
}

& $qemu @vmArgs
exit $LASTEXITCODE
