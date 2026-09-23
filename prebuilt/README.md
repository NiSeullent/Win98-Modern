# Local prebuilt Windows 98 VM

`build_local.py` creates a **local-only full VirtualBox clone** of an already
installed, licensed Windows 98 SE VM. It does not install Windows, inject a CD
key, start a guest, or alter the source VM. Its default action is a read-only
preflight. The default source VM is `Win98Modern-Base`; it is an
installed Windows 98 SE base image, so its clone is **not yet a verified,
fully patched Shizuku's Second Edition**. Apply and test the project's patch
set on the independent clone before giving it that label.

On 2026-09-24, the powered-off patched `Win98Modern-Accel-128` was used to
build a local preview clone named `Win98-Shizuku-SE-Preview-20260924-v2`.
The clone has its own base VDI and COM1 pipe, booted to the Windows 98 GUI on
the current WHPX/NEM hardware backend, answered the COM1 agent, and passed
the installed `KERNEL32.GetFinalPathNameByHandleW` static import and 41-entry
sampled API-table tests. Its initial clone manifest and separate runtime
evidence are under its ignored `prebuilt/local/` directory. This is a tested
**patch preview**; the local VDI is not published.

## Build locally

From the repository root on a host with Python 3.10+ and VirtualBox 7.2:

```powershell
py -3 prebuilt/build_local.py
py -3 prebuilt/build_local.py --build
```

The first command checks the powered-off source, BIOS/PIIX3/PIIX4 IDE layout,
`hwvirtex=on`, host hardware virtualization support, the complete source disk
chain, destination collision, and free space. It hashes the source
configuration and disk without creating a VM or output file. The second
command performs the same checks, then calls `VBoxManage clonevm` with
`--mode=machine --register` and no `Link` option. The result is under
`prebuilt/local/Win98-Shizuku-SE-Local/` and is ignored by Git. Set
`--output-root` to a directory outside the repository if desired.

The source can instead be a powered-on VM **only when** an existing,
named, powered-off-state snapshot is selected. The tool checks the exact
snapshot UUID and rejects saved/running-state snapshots:

```powershell
py -3 prebuilt/build_local.py --source Win98Modern-Accel-128 --snapshot before-com-recovery-20260923
py -3 prebuilt/build_local.py --source Win98Modern-Accel-128 --snapshot before-com-recovery-20260923 --build
```

Choose a unique `--clone-name` for each build. The tool does not create a
snapshot or restore one. A source snapshot can represent older guest content;
inspect and test the clone before treating it as current.

The clone's DVD/floppy media are emptied without changing the source. If the
source used a server named pipe for a guest COM port, the clone gets a unique
pipe name derived from its VM name and UUID. Other externally connected COM
ports are disconnected from the host while keeping the guest port present. The
output `prebuilt-manifest.json` records source VM and snapshot UUIDs,
VirtualBox version, exact source configuration and disk-chain paths with
SHA-256 hashes, clone configuration and disk hashes, and the verified VM
settings. The disk check requires a single independent base disk inside the
clone directory. If validation fails after cloning, the tool leaves the clone
for inspection and does not write a success manifest.
VirtualBox may remove its own transient, read-only guest properties from the
powered-off source `.vbox` while cloning. The final check permits only that
metadata change: it still hashes each source disk again, requires the source
to remain powered off, and compares all other configuration XML. The manifest
keeps both the preflight and completion configuration hashes.

## Hardware acceleration and boot

The preflight checks host virtualization capability and the VM setting.
Those checks cannot prove the execution backend of a VM that has not been
started. The manifest therefore sets `runtime_acceleration_verified` to
`false`. After intentionally starting the clone, inspect **its current**
`VBox.log` for an HM VT-x/AMD-V backend or the WHPX/NEM hardware backend
(`NEM: Created partition`). A stale log or `hwvirtex=on` alone is insufficient.
The clone remains on BIOS/CSM, PIIX3 and IDE/PIIX4. This script does not claim
native UEFI boot, SATA/xHCI drivers, PAE, or successful modern app execution.

## Publication boundary

The local VM disk contains Microsoft binaries and may contain the installed
product key. Do not commit, attach to a release, or publish the VM directory,
disk, ISO, manifest, or guest logs. The script never accepts, prints, or stores
a product key and suppresses raw VirtualBox errors. Before any public source
release, inspect staged paths with `git diff --cached --name-only`; only the
script, documentation, and tests in this directory are intended for GitHub.

The source patch bundle and this local prebuilt VM are separate distribution
paths. The public patch bundle must continue to exclude Microsoft files.
VirtualBox's [full clone documentation](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/EN-VBOX-7-2-USER.pdf)
describes why a full clone can run without the source disk.

## Tests

```powershell
py -3 -m unittest discover -s prebuilt/tests -v
py -3 prebuilt/build_local.py
```

The unit tests use tiny synthetic VM configurations and never invoke
`VBoxManage clonevm`. The second command is a host-specific, read-only
preflight against the installed lab VM.
