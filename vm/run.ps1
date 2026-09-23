# Start the verified Windows 98 VM through VirtualBox's hardware virtualization backend.

param(
  [string]$VmName = 'Win98Modern-Accel-128'
)

$ErrorActionPreference = 'Stop'
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$vmName = $VmName
$vmFolder = Join-Path $PSScriptRoot "accel\$vmName"
$log = Join-Path $vmFolder 'Logs\VBox.log'

if (-not (Test-Path -LiteralPath $vbox)) { throw "VirtualBox is missing: $vbox" }

$info = & $vbox showvminfo $vmName --machinereadable
if ($LASTEXITCODE -ne 0) { throw "VirtualBox VM is missing: $vmName" }
if ($info -match '^VMState="running"$') {
  Write-Host "$vmName is already running."
} elseif ($info -match '^VMState="paused"$') {
  & $vbox controlvm $vmName resume
  if ($LASTEXITCODE -ne 0) { throw "Could not resume $vmName" }
} else {
  & $vbox startvm $vmName --type headless
  if ($LASTEXITCODE -ne 0) { throw "Could not start $vmName" }
}

Start-Sleep -Seconds 2
if (-not (Test-Path -LiteralPath $log)) { throw "Acceleration log is missing: $log" }
$accelerated = Select-String -LiteralPath $log -Pattern 'NEM: Created partition' -Quiet
if (-not $accelerated) {
  throw "Hardware virtualization was not confirmed in $log"
}
$currentInfo = & $vbox showvminfo $vmName --machinereadable
if ($LASTEXITCODE -ne 0 -or -not ($currentInfo -match '^VMState="running"$')) {
  throw "$vmName is not running after the acceleration check."
}
Write-Host "$vmName is running with a hardware virtualization backend."
