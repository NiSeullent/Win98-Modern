param(
  [string]$VmName = 'Win98Modern-Accel-128',
  [string]$PipeName = '\\.\pipe\Win98Modern-Lab',
  [switch]$Disable
)

$ErrorActionPreference = 'Stop'
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($VmName -eq 'Win98Modern-Base' -or $VmName -notlike 'Win98Modern-*') {
  throw 'Use a disposable Win98Modern test VM; the preserved Base is forbidden.'
}
if (-not $PipeName.StartsWith('\\.\pipe\') -or $PipeName.Length -le 9) {
  throw 'The serial endpoint must be a local Windows named pipe.'
}
$info = & $vbox showvminfo $VmName --machinereadable
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the test VM.' }
if (-not ($info -match '^VMState="poweroff"$')) {
  throw 'Shut down the guest normally before configuring its serial port.'
}
$evidence = Join-Path $projectRoot 'build/remote'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$backup = Join-Path $evidence ("serial-before-{0}.txt" -f [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$info | Set-Content -LiteralPath $backup -Encoding utf8
if ($Disable) {
  & $vbox modifyvm $VmName --uart1 off
} else {
  & $vbox modifyvm $VmName --uart1 0x3f8 4 --uart-mode1 server $PipeName --uart-type1 16550A
}
if ($LASTEXITCODE -ne 0) { throw 'Serial configuration failed.' }
$after = & $vbox showvminfo $VmName --machinereadable
if ($LASTEXITCODE -ne 0) { throw 'Cannot verify serial configuration.' }
$after | Select-String '^uart1=|^uartmode1=|^uarttype1='
Write-Host "Previous settings saved: $backup"
