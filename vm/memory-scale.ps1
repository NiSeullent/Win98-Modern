# Disposable, full-clone-only Win98 RAM and PAE experiment.
param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('Preflight', 'Prepare', 'Start', 'StopFirmware', 'Capture', 'Finish', 'Status')]
  [string]$Action,
  [ValidateSet(512, 1024, 2048, 3072, 4096)]
  [int]$MemoryMiB = 512,
  [ValidateSet('Off', 'On')]
  [string]$Pae = 'Off',
  [ValidateSet('Windows', 'Firmware')]
  [string]$BootMode = 'Windows',
  [ValidatePattern('^[a-zA-Z0-9_-]{1,32}$')]
  [string]$Label = 'desktop',
  [string]$GuestObservation = '',
  [ValidateSet('Normal', 'ForcedAfterStall', 'FirmwareProbeStop')]
  [string]$ShutdownKind = 'Normal'
)

$ErrorActionPreference = 'Stop'
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$sourceName = 'Win98Modern-Base'
$cloneName = "Win98Modern-Mem-$MemoryMiB-PAE-$Pae"
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'accel\memory-tests'))
$cloneFolder = Join-Path $testRoot $cloneName
$media = Join-Path $testRoot 'memory-probes.iso'
$floppy = Join-Path $testRoot 'e820-autoboot.img'
$manifest = Join-Path $cloneFolder 'memory-test.json'

if (-not (Test-Path -LiteralPath $vbox -PathType Leaf)) { throw "VirtualBox is missing: $vbox" }

function Invoke-VBox([string[]]$Arguments) {
  $lines = @(& $script:vbox @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) {
    throw "VBoxManage $($Arguments -join ' ') failed: $($lines -join ' ')"
  }
  return $lines
}

function Get-MachineValue([string[]]$Lines, [string]$Key) {
  $found = @($Lines | Where-Object { $_ -match "^$([regex]::Escape($Key))=" })
  if ($found.Count -ne 1) { throw "Expected one $Key field in VirtualBox output." }
  $raw = $found[0].Substring($Key.Length + 1)
  # Machine-readable paths contain JSON-style escaped backslashes.
  if ($raw.StartsWith('"')) { return ($raw | ConvertFrom-Json) }
  return $raw
}

function Get-VmInfo([string]$Name) {
  $lines = @(Invoke-VBox -Arguments @('showvminfo', $Name, '--machinereadable'))
  return [pscustomobject]@{
    Name = $Name
    Uuid = Get-MachineValue $lines 'UUID'
    State = Get-MachineValue $lines 'VMState'
    MemoryMiB = [int](Get-MachineValue $lines 'memory')
    Pae = Get-MachineValue $lines 'pae'
    NestedPaging = Get-MachineValue $lines 'nestedpaging'
    Nic1 = Get-MachineValue $lines 'nic1'
    Config = Get-MachineValue $lines 'CfgFile'
    Disk = Get-MachineValue $lines '"IDE-0-0"'
    Optical = Get-MachineValue $lines '"IDE-1-0"'
  }
}

function Get-MediumField([string[]]$Lines, [string]$Field) {
  $pattern = "^$([regex]::Escape($Field)):\s*"
  $found = @($Lines | Where-Object { $_ -match $pattern })
  if ($found.Count -ne 1) { throw "Expected one $Field field in medium information." }
  return ($found[0] -replace $pattern, '').Trim()
}

function Get-MediumInfo([string]$Disk) {
  $lines = @(Invoke-VBox -Arguments @('showmediuminfo', 'disk', $Disk))
  return [pscustomobject]@{
    Uuid = Get-MediumField $lines 'UUID'
    ParentUuid = Get-MediumField $lines 'Parent UUID'
    Type = Get-MediumField $lines 'Type'
    Location = Get-MediumField $lines 'Location'
    InUse = Get-MediumField $lines 'In use by VMs'
  }
}

function Assert-Within([string]$Path, [string]$Folder) {
  $fullPath = [IO.Path]::GetFullPath($Path)
  $prefix = [IO.Path]::GetFullPath($Folder).TrimEnd('\') + '\'
  if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Experiment file is outside $Folder`: $Path"
  }
}

function Assert-CloneIsolated($Source, $Clone) {
  if ($Source.Uuid -eq $Clone.Uuid) { throw 'Clone and Base share a VM UUID.' }
  Assert-Within $Clone.Config $cloneFolder
  Assert-Within $Clone.Disk $cloneFolder
  if (-not (Test-Path -LiteralPath $Clone.Disk -PathType Leaf)) {
    throw "Clone disk is missing: $($Clone.Disk)"
  }
  $sourceDisk = Get-MediumInfo $Source.Disk
  $cloneDisk = Get-MediumInfo $Clone.Disk
  if ($cloneDisk.Uuid -eq $sourceDisk.Uuid -or
      $cloneDisk.ParentUuid -ne 'base' -or
      $cloneDisk.Type -ne 'normal (base)' -or
      $cloneDisk.InUse -notlike "$($Clone.Name)*") {
    throw 'Clone disk is shared, linked, or not a normal independent base medium.'
  }
  if (-not [string]::Equals([IO.Path]::GetFullPath($cloneDisk.Location),
                           [IO.Path]::GetFullPath($Clone.Disk),
                           [StringComparison]::OrdinalIgnoreCase)) {
    throw 'VirtualBox medium location differs from the attached clone disk.'
  }
  return $cloneDisk
}

function Get-HostHeadroom {
  Add-Type -AssemblyName Microsoft.VisualBasic
  $computer = [Microsoft.VisualBasic.Devices.ComputerInfo]::new()
  $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($testRoot))
  return [pscustomobject]@{
    FreePhysicalMiB = [int][math]::Floor($computer.AvailablePhysicalMemory / 1MB)
    FreeDiskMiB = [int][math]::Floor($drive.AvailableFreeSpace / 1MB)
  }
}

function Read-Manifest {
  if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Experiment manifest is missing: $manifest"
  }
  $record = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
  if ($record.CloneName -ne $cloneName -or
      $record.ConfiguredMemoryMiB -ne $MemoryMiB -or
      $record.ConfiguredPae -ne $Pae) {
    throw 'Experiment manifest does not match the requested RAM and PAE settings.'
  }
  return $record
}

function Get-GuestRegisters([string]$Name) {
  $lines = @(Invoke-VBox -Arguments @('debugvm', $Name, 'getregisters', 'cr0', 'cr3', 'cr4'))
  $cr0Line = @($lines | Where-Object { $_ -match '^cr0 = 0x([0-9a-fA-F]+)$' })
  $cr4Line = @($lines | Where-Object { $_ -match '^cr4 = 0x([0-9a-fA-F]+)$' })
  if ($cr0Line.Count -ne 1 -or $cr4Line.Count -ne 1) {
    throw 'VirtualBox did not return guest CR0 and CR4 registers.'
  }
  $cr0 = [Convert]::ToUInt64(($cr0Line[0] -replace '^cr0 = 0x', ''), 16)
  $cr4 = [Convert]::ToUInt64(($cr4Line[0] -replace '^cr4 = 0x', ''), 16)
  return [pscustomobject]@{
    Raw = $lines
    Cr0PagingEnabled = [bool](($cr0 -band [uint64]2147483648) -ne 0)
    Cr4PaeEnabled = [bool](($cr4 -band [uint64]32) -ne 0)
  }
}

$source = Get-VmInfo $sourceName
if ($Action -eq 'Preflight') {
  $headroom = Get-HostHeadroom
  Write-Host "Base state=$($source.State) RAM=$($source.MemoryMiB)MiB PAE=$($source.Pae)"
  Write-Host "Host free RAM=$($headroom.FreePhysicalMiB)MiB disk=$($headroom.FreeDiskMiB)MiB"
  Write-Host "Probe CD present=$(Test-Path -LiteralPath $media -PathType Leaf)"
  Write-Host "Firmware probe floppy present=$(Test-Path -LiteralPath $floppy -PathType Leaf)"
  Write-Host "Running VMs: $(@(Invoke-VBox -Arguments @('list', 'runningvms')) -join ', ')"
  return
}

if ($Action -eq 'Prepare') {
  if ($source.State -ne 'poweroff') { throw 'Base VM must be powered off before cloning.' }
  if (Test-Path -LiteralPath $cloneFolder) { throw "Experiment already exists: $cloneFolder" }
  if (-not (Test-Path -LiteralPath $media -PathType Leaf)) {
    throw "Build the memory probe CD first: $media"
  }
  if (-not (Test-Path -LiteralPath $source.Disk -PathType Leaf)) {
    throw "Base disk is missing: $($source.Disk)"
  }
  $registered = @(Invoke-VBox -Arguments @('list', 'vms'))
  if ($registered -match "^`"$([regex]::Escape($cloneName))`" ") {
    throw "A VM named $cloneName is already registered."
  }
  $headroom = Get-HostHeadroom
  $minDiskMiB = [int][math]::Ceiling((Get-Item -LiteralPath $source.Disk).Length / 1MB) + 1024
  if ($headroom.FreeDiskMiB -lt $minDiskMiB) {
    throw "Need at least $minDiskMiB MiB free disk to clone with a 1 GiB reserve."
  }
  $sourceHash = (Get-FileHash -LiteralPath $source.Disk -Algorithm SHA256).Hash
  $sourceMedium = Get-MediumInfo $source.Disk
  $mediaHash = (Get-FileHash -LiteralPath $media -Algorithm SHA256).Hash
  New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
  # --mode machine without --options Link copies the current state in full.
  Invoke-VBox -Arguments @('clonevm', $sourceName, '--name', $cloneName,
                          '--basefolder', $testRoot, '--mode', 'machine', '--register') | Out-Null
  $clone = Get-VmInfo $cloneName
  $cloneMedium = Assert-CloneIsolated $source $clone
  Invoke-VBox -Arguments @('modifyvm', $cloneName, '--memory', "$MemoryMiB",
                          '--pae', $Pae.ToLowerInvariant(), '--nic1', 'none',
                          '--clipboard-mode', 'disabled', '--draganddrop', 'disabled') | Out-Null
  Invoke-VBox -Arguments @('storageattach', $cloneName, '--storagectl', 'IDE',
                          '--port', '1', '--device', '0', '--type', 'dvddrive',
                          '--medium', $media) | Out-Null
  $clone = Get-VmInfo $cloneName
  $cloneMedium = Assert-CloneIsolated $source $clone
  if ($clone.MemoryMiB -ne $MemoryMiB -or $clone.Pae -ne $Pae.ToLowerInvariant() -or
      $clone.Nic1 -ne 'none' -or
      -not [string]::Equals($clone.Optical, $media, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Clone RAM, PAE, network, or probe CD configuration did not stick.'
  }
  if ((Get-FileHash -LiteralPath $source.Disk -Algorithm SHA256).Hash -ne $sourceHash) {
    throw 'Base disk changed during cloning.'
  }
  [pscustomobject]@{
    PreparedUtc = (Get-Date).ToUniversalTime().ToString('o')
    SourceName = $sourceName
    SourceUuid = $source.Uuid
    SourceDisk = $source.Disk
    SourceDiskUuid = $sourceMedium.Uuid
    SourceDiskSha256 = $sourceHash
    CloneName = $cloneName
    CloneUuid = $clone.Uuid
    CloneDisk = $clone.Disk
    CloneDiskUuid = $cloneMedium.Uuid
    CloneDiskParentUuid = $cloneMedium.ParentUuid
    ConfiguredMemoryMiB = $MemoryMiB
    ConfiguredPae = $Pae
    ProbeIso = $media
    ProbeIsoSha256 = $mediaHash
    InitialHostHeadroom = $headroom
    Interpretation = 'PAE flag exposes a CPU feature; it does not prove the guest enables PAE or uses 4 GiB.'
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifest -Encoding UTF8
  Write-Host "Prepared independent $MemoryMiB MiB, PAE $Pae VM: $cloneName"
  Write-Host "Evidence manifest: $manifest"
  return
}

$record = Read-Manifest
$clone = Get-VmInfo $cloneName
$cloneMedium = Assert-CloneIsolated $source $clone
if ($source.Uuid -ne $record.SourceUuid -or
    $clone.Uuid -ne $record.CloneUuid -or
    $cloneMedium.Uuid -ne $record.CloneDiskUuid -or
    $clone.MemoryMiB -ne $MemoryMiB -or
    $clone.Pae -ne $Pae.ToLowerInvariant() -or
    $clone.Nic1 -ne 'none' -or
    -not [string]::Equals($clone.Optical, $record.ProbeIso, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Experiment configuration differs from its preparation manifest.'
}

if ($Action -eq 'Start') {
  if ($source.State -ne 'poweroff') { throw 'Base VM must stay powered off.' }
  if ($clone.State -ne 'poweroff') { throw "Clone must be powered off; state=$($clone.State)." }
  $running = @(Invoke-VBox -Arguments @('list', 'runningvms'))
  if ($running.Count -gt 0) { throw "Another VM is running: $($running -join ', ')" }
  if ((Get-FileHash -LiteralPath $source.Disk -Algorithm SHA256).Hash -ne $record.SourceDiskSha256) {
    throw 'Base disk hash changed since preparation.'
  }
  if ((Get-FileHash -LiteralPath $media -Algorithm SHA256).Hash -ne $record.ProbeIsoSha256) {
    throw 'Probe CD hash changed since preparation.'
  }
  $headroom = Get-HostHeadroom
  if ($headroom.FreePhysicalMiB -lt ($MemoryMiB + 4096)) {
    throw "Host free RAM $($headroom.FreePhysicalMiB) MiB is below guest RAM plus 4 GiB reserve."
  }
  $floppyHash = $null
  if ($BootMode -eq 'Firmware') {
    if (-not (Test-Path -LiteralPath $floppy -PathType Leaf)) {
      throw "Build the E820 probe floppy first: $floppy"
    }
    $floppyHash = (Get-FileHash -LiteralPath $floppy -Algorithm SHA256).Hash
    Invoke-VBox -Arguments @('storageattach', $cloneName, '--storagectl', 'Floppy',
                            '--port', '0', '--device', '0', '--type', 'fdd',
                            '--medium', $floppy) | Out-Null
    Invoke-VBox -Arguments @('modifyvm', $cloneName, '--boot1', 'floppy',
                            '--boot2', 'disk', '--boot3', 'none',
                            '--nested-paging', 'on') | Out-Null
  } else {
    Invoke-VBox -Arguments @('modifyvm', $cloneName, '--boot1', 'disk',
                            '--boot2', 'dvd', '--boot3', 'none',
                            '--nested-paging', 'off') | Out-Null
  }
  $clone = Get-VmInfo $cloneName
  $expectedNestedPaging = if ($BootMode -eq 'Firmware') { 'on' } else { 'off' }
  if ($clone.NestedPaging -ne $expectedNestedPaging) {
    throw "Nested paging setting did not stick for $BootMode mode."
  }
  Invoke-VBox -Arguments @('startvm', $cloneName, '--type', 'headless') | Out-Null
  Start-Sleep -Seconds 3
  $clone = Get-VmInfo $cloneName
  $log = Join-Path $cloneFolder 'Logs\VBox.log'
  $nem = (Test-Path -LiteralPath $log -PathType Leaf) -and
         (Select-String -LiteralPath $log -Pattern 'NEM: Created partition' -Quiet)
  $startPath = Join-Path $cloneFolder ("start-" + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '.json')
  [pscustomobject]@{
    StartedUtc = (Get-Date).ToUniversalTime().ToString('o')
    CloneName = $cloneName
    CloneStateAfterStart = $clone.State
    ConfiguredMemoryMiB = $MemoryMiB
    ConfiguredPae = $Pae
    BootMode = $BootMode
    FirmwareProbeFloppySha256 = $floppyHash
    NemPartitionSeenInLog = [bool]$nem
    HostHeadroomBeforeStart = $headroom
    Interpretation = 'VirtualBox configuration and NEM log do not show whether Win98 enables CR4.PAE.'
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $startPath -Encoding UTF8
  if ($clone.State -ne 'running') { throw "Clone failed to remain running: $($clone.State)" }
  Write-Host "$cloneName is running (NEM partition log present=$nem)."
  if ($BootMode -eq 'Firmware') {
    Write-Host 'The E820 boot sector prints the firmware map automatically; capture its completed screen.'
    Write-Host 'After the capture, stop this read-only probe session and record -ShutdownKind FirmwareProbeStop.'
  } else {
    Write-Host 'In Windows 98, run D:\MEMPROBE.EXE then D:\MEMSTRS.EXE 8 1; capture each result.'
    Write-Host 'Copy D:\E820.COM to C:\E820.COM in Windows first; run C:\E820.COM in real DOS mode.'
  }
  return
}

if ($Action -eq 'StopFirmware') {
  if ($BootMode -ne 'Firmware') { throw 'StopFirmware requires -BootMode Firmware.' }
  if ($clone.State -ne 'running') { throw "Firmware clone is not running; state=$($clone.State)." }
  $log = Join-Path $cloneFolder 'Logs\VBox.log'
  if (-not (Test-Path -LiteralPath $log -PathType Leaf) -or
      -not (Select-String -LiteralPath $log -Pattern 'Booting from Floppy' -Quiet)) {
    throw 'Current VM log does not confirm a floppy boot.'
  }
  $registers = Get-GuestRegisters $cloneName
  if ($registers.Cr0PagingEnabled) {
    throw 'Guest is in paged execution; refusing a firmware-probe stop.'
  }
  $latestStartFile = Get-ChildItem -LiteralPath $cloneFolder -Filter 'start-*.json' |
                     Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
  if (-not $latestStartFile) { throw 'Missing firmware probe start evidence.' }
  $latestStart = Get-Content -LiteralPath $latestStartFile.FullName -Raw | ConvertFrom-Json
  if ($latestStart.BootMode -ne 'Firmware' -or
      -not $latestStart.FirmwareProbeFloppySha256) {
    throw 'Latest start record is not a firmware probe.'
  }
  Invoke-VBox -Arguments @('controlvm', $cloneName, 'poweroff') | Out-Null
  $clone = Get-VmInfo $cloneName
  $afterHash = (Get-FileHash -LiteralPath $floppy -Algorithm SHA256).Hash
  if ($clone.State -ne 'poweroff' -or
      $afterHash -ne $latestStart.FirmwareProbeFloppySha256) {
    throw 'Firmware probe stop or floppy hash verification failed.'
  }
  $stopPath = Join-Path $cloneFolder ("firmware-stop-" + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '.json')
  [pscustomobject]@{
    StoppedUtc = (Get-Date).ToUniversalTime().ToString('o')
    CloneName = $cloneName
    StopKind = 'Autonomous real-mode E820 probe; host stopped disposable VM'
    GuestRegistersBeforeStop = $registers
    FirmwareProbeFloppySha256StillMatches = $true
    GuestObservation = $GuestObservation
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $stopPath -Encoding UTF8
  Write-Host "Stopped read-only firmware probe: $stopPath"
  return
}

if ($Action -eq 'Capture') {
  if ($clone.State -ne 'running') { throw "Capture requires a running clone; state=$($clone.State)." }
  $screen = Join-Path $cloneFolder "capture-$Label.png"
  $capture = Join-Path $cloneFolder "capture-$Label.json"
  if ((Test-Path -LiteralPath $screen) -or (Test-Path -LiteralPath $capture)) {
    throw "Capture label already exists: $Label"
  }
  $guestRegisters = Get-GuestRegisters $cloneName
  Invoke-VBox -Arguments @('controlvm', $cloneName, 'screenshotpng', $screen) | Out-Null
  [pscustomobject]@{
    CapturedUtc = (Get-Date).ToUniversalTime().ToString('o')
    CloneName = $cloneName
    ConfiguredMemoryMiB = $clone.MemoryMiB
    ConfiguredPae = $Pae
    GuestObservation = $GuestObservation
    Screenshot = $screen
    ScreenshotSha256 = (Get-FileHash -LiteralPath $screen -Algorithm SHA256).Hash
    GuestRegisters = $guestRegisters
    HostHeadroom = Get-HostHeadroom
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $capture -Encoding UTF8
  Write-Host "Captured $screen"
  return
}

if ($Action -eq 'Finish') {
  if ($clone.State -ne 'poweroff') { throw "Power off the clone before Finish; state=$($clone.State)." }
  if ((Get-FileHash -LiteralPath $source.Disk -Algorithm SHA256).Hash -ne $record.SourceDiskSha256) {
    throw 'Base disk hash changed during the experiment.'
  }
  $log = Join-Path $cloneFolder 'Logs\VBox.log'
  $finishPath = Join-Path $cloneFolder 'finish.json'
  if (Test-Path -LiteralPath $finishPath) { throw "Finish evidence already exists: $finishPath" }
  $apmShutdownSeen = (Test-Path -LiteralPath $log -PathType Leaf) -and
                     (Select-String -LiteralPath $log -Pattern 'PcBios: APM shutdown request' -Quiet)
  $floppyBootSeen = (Test-Path -LiteralPath $log -PathType Leaf) -and
                    (Select-String -LiteralPath $log -Pattern 'Booting from Floppy' -Quiet)
  if ($ShutdownKind -eq 'Normal' -and -not $apmShutdownSeen) {
    throw 'No APM shutdown request in VBox.log; use -ShutdownKind ForcedAfterStall with stall evidence.'
  }
  if ($ShutdownKind -eq 'ForcedAfterStall' -and
      -not (Test-Path -LiteralPath (Join-Path $cloneFolder 'shutdown-cpu.txt') -PathType Leaf)) {
    throw 'Forced-after-stall finish requires shutdown-cpu.txt evidence.'
  }
  if ($ShutdownKind -eq 'FirmwareProbeStop' -and -not $floppyBootSeen) {
    throw 'Firmware probe stop requires a floppy boot in VBox.log.'
  }
  if ($ShutdownKind -eq 'FirmwareProbeStop' -and
      @(Get-ChildItem -LiteralPath $cloneFolder -Filter 'firmware-stop-*.json').Count -eq 0) {
    throw 'Firmware probe stop requires a firmware-stop JSON record.'
  }
  [pscustomobject]@{
    FinishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    CloneName = $cloneName
    FinalState = $clone.State
    ConfiguredMemoryMiB = $clone.MemoryMiB
    ConfiguredPae = $Pae
    ShutdownKind = $ShutdownKind
    ApmShutdownRequestSeen = [bool]$apmShutdownSeen
    FloppyBootSeen = [bool]$floppyBootSeen
    SourceDiskSha256StillMatches = $true
    VBoxLog = if (Test-Path -LiteralPath $log) { $log } else { $null }
    VBoxLogSha256 = if (Test-Path -LiteralPath $log) { (Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash } else { $null }
    GuestObservation = $GuestObservation
    Interpretation = 'Guest boot, E820, or page readback alone do not establish PAE use or 4 GiB physical RAM use.'
  } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $finishPath -Encoding UTF8
  Write-Host "Finished evidence: $finishPath"
  return
}

$headroom = Get-HostHeadroom
Write-Host "$cloneName state=$($clone.State) RAM=$($clone.MemoryMiB)MiB PAE=$($clone.Pae)"
Write-Host "clone_disk=$($clone.Disk) clone_medium_uuid=$($cloneMedium.Uuid) parent=$($cloneMedium.ParentUuid)"
Write-Host "source_disk=$($source.Disk) host_free_ram_mib=$($headroom.FreePhysicalMiB)"
