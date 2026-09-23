param(
  [Parameter(Mandatory = $true)]
  [string]$InstallationMedia
)

$ErrorActionPreference = 'Stop'
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$vmName = 'Win98Modern-Accel-128'
$baseFolder = Join-Path $PSScriptRoot 'accel'
$disk = Join-Path (Join-Path $baseFolder $vmName) "$vmName.vdi"

if (-not (Test-Path -LiteralPath $vbox)) { throw "VirtualBox is missing: $vbox" }
if (-not (Test-Path -LiteralPath $InstallationMedia)) { throw 'Installation media does not exist.' }
$mediaPath = (Resolve-Path -LiteralPath $InstallationMedia).Path

$registered = & $vbox list vms
if ($registered -match "^`"$vmName`" ") { throw "$vmName is already registered; existing disks will not be changed." }

& $vbox createvm --name $vmName --basefolder $baseFolder --ostype Windows98 --register
if ($LASTEXITCODE -ne 0) { throw 'VM creation failed.' }

& $vbox modifyvm $vmName --memory 128 --cpus 1 --chipset piix3 --firmware bios --graphicscontroller vboxvga --vram 8 --audio-driver none --nic1 none --usb off --ioapic off --pae off --nested-paging off --paravirtprovider none --boot1 dvd --boot2 disk --boot3 none --boot4 none
if ($LASTEXITCODE -ne 0) { throw 'VM configuration failed.' }

& $vbox createmedium disk --filename $disk --size 4096 --format VDI --variant Standard
if ($LASTEXITCODE -ne 0) { throw 'VM disk creation failed.' }

& $vbox storagectl $vmName --name IDE --add ide --controller PIIX4
if ($LASTEXITCODE -ne 0) { throw 'IDE controller creation failed.' }

& $vbox storageattach $vmName --storagectl IDE --port 0 --device 0 --type hdd --medium $disk
if ($LASTEXITCODE -ne 0) { throw 'VM disk attachment failed.' }

& $vbox storageattach $vmName --storagectl IDE --port 1 --device 0 --type dvddrive --medium $mediaPath
if ($LASTEXITCODE -ne 0) { throw 'Installation media attachment failed.' }

Write-Host "$vmName is configured. Start it with .\run.ps1 and complete setup interactively."
