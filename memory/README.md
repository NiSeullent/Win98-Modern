# PAE and physical RAM workstream for CSM systems

The current completion target is to use all usable physical RAM exposed by a modern CSM-capable system, including RAM above the 4 GiB address boundary. The 512 MiB and 4 GiB virtual machines below are controlled milestones, not proof of that target. No Win98 PAE memory-manager implementation exists yet.

`memprobe.exe` reports physical memory exposed by `GlobalMemoryStatus` and, if available, `GlobalMemoryStatusEx`. `memstress.exe` makes bounded allocations in one or more simultaneous processes, writes a page-specific four-byte pattern across every 4 KiB page, then checks each page again after all workers have touched their allocations. Workers hold their allocations until the parent samples the overlap. Both are measurement tools, not a 4GB patch.

## Run the stress tool

At a Windows 98 command prompt, run `memstress.exe [MiB_per_worker] [workers]`. The default is `memstress.exe 16 1`. For a modest cross-process check, run `memstress.exe 64 4`. Arguments are decimal integers. Limits: 1–768 MiB per worker, 1–8 workers, and at most 3584 MiB requested in total. Invalid sizes fail before any allocation. The program only allocates virtual memory and creates temporary processes and events; it changes no files or OS settings. Workers wait up to 10 minutes for the parent to release them, so a failed parent will not leave them allocated indefinitely.

The output has three parts:

1. `parent_before`, `parent_during_overlap`, and `parent_after_release` show the legacy Windows physical/page-file reports in **bytes**, in hexadecimal. The 32-bit `GlobalMemoryStatus` fields can saturate or wrap at large sizes; compare with `memprobe.exe` and do not treat these fields alone as a trustworthy 4GB reading.
2. Each `worker=` line reports its requested and allocated MiB, the number of 4 KiB pages read back correctly after all workers reached the verification barrier, and any allocation or verification error. Workers keep these pages allocated until the parent releases them. A partial allocation is reported as partial, not rounded up.
3. `result` reports the verified sum, the number of ready workers, and how many worker processes remained alive together when the parent sampled. `result_status=ALL_REQUESTED_PAGES_VERIFIED` and exit code 0 require all workers to finish and verify their requested pages while they are alive together. Any shortfall gives `INCOMPLETE` and exit code 2.

**Interpretation limit:** Successful allocation, page writes, and readback prove that the tested virtual pages were usable by those processes during the test. They do **not** prove that all pages were resident in physical RAM at the same instant. Paging or swap can satisfy the test, and a modern host can make Windows 98's legacy physical-memory fields saturate. Keep the VM's backing configuration and host memory/swap telemetry with results. Do not report 4GB physical support from this tool alone.

## Disposable RAM and PAE experiments

`memory/build-media.py` builds `vm/accel/memory-tests/memory-probes.iso` from the two Win98 executables and the 16-bit DOS `E820.COM` probe. Build the executables with `build.ps1` first. NASM and `pycdlib` are required. `memory/build-e820-autoboot.py` builds a bootable floppy that prints the E820 map automatically without DOS or keyboard input. It needs NASM and no external DOS binaries. The builds refuse to overwrite existing media so experiment hashes stay meaningful. The media contain no Windows installation files.

`vm/memory-scale.ps1` creates **full, independent clones** of the powered-off `Win98Modern-Base` VM. It rejects linked media by checking the cloned VDI's UUID, `Parent UUID: base`, medium type, registered owner, and location inside the experiment directory. It also records the Base disk hash, leaves the Base VM and existing app-test VM untouched, disables networking, and refuses to start when another VM runs or the host has less than guest RAM plus 4 GiB free. All clone data and evidence stay under the ignored `vm/accel/memory-tests/` directory.

Run one configuration at a time from the project root:

```powershell
./vm/memory-scale.ps1 -Action Preflight
./vm/memory-scale.ps1 -Action Prepare -MemoryMiB 512 -Pae Off
./vm/memory-scale.ps1 -Action Start -MemoryMiB 512 -Pae Off
./vm/memory-scale.ps1 -Action Capture -MemoryMiB 512 -Pae Off -Label memprobe -GuestObservation 'Exact guest output'
./vm/memory-scale.ps1 -Action Status -MemoryMiB 512 -Pae Off
./vm/memory-scale.ps1 -Action Finish -MemoryMiB 512 -Pae Off -GuestObservation 'Normal guest shutdown result'
```

To read the firmware map when Windows cannot boot, run the same full clone from the probe floppy after all other VMs are off:

```powershell
python memory/build-e820-autoboot.py
./vm/memory-scale.ps1 -Action Start -MemoryMiB 4096 -Pae On -BootMode Firmware
# The boot sector prints E820 ranges automatically.
./vm/memory-scale.ps1 -Action Capture -MemoryMiB 4096 -Pae On -Label e820-firmware
./vm/memory-scale.ps1 -Action StopFirmware -MemoryMiB 4096 -Pae On -BootMode Firmware
# If this is the only phase for the clone:
./vm/memory-scale.ps1 -Action Finish -MemoryMiB 4096 -Pae On -ShutdownKind FirmwareProbeStop
```

`BootMode Firmware` enables nested paging for the real-mode probe; `BootMode Windows` restores the Base VM's nested-paging-off setting before Windows starts. `StopFirmware` only targets the disposable clone after confirming a floppy boot and real-mode vCPU state, then verifies that the floppy hash is unchanged. The autonomous boot sector has no file-write code; the stop does not represent a successful Windows shutdown. To test Windows in that clone after the firmware capture, skip `Finish`, start it with `-BootMode Windows`, and write the single final finish record after the Windows phase.

`Finish` requires the clone to be powered off. The default `-ShutdownKind Normal` requires an APM shutdown request in `VBox.log`. If a captured stall led to a forced stop, save the CPU instruction/register evidence as `shutdown-cpu.txt` in the clone folder and use `-ShutdownKind ForcedAfterStall`; the finish record marks that outcome explicitly. A prepared clone is never silently replaced; choose a new configuration or retain and inspect the existing clone. Supported RAM settings are 512, 1024, 2048, 3072, and 4096 MiB, each with PAE `On` or `Off`. At each setting capture boot behavior, `D:\MEMPROBE.EXE`, `D:\MEMSTRS.EXE 8 1`, guest `SYSTEM.INI` changes if any, and shutdown/reboot behavior. Increase stress gradually only after a small run is stable. Before selecting **Restart in MS-DOS mode**, copy `D:\E820.COM` to `C:\E820.COM` in the disposable clone; this VM's CD driver is unavailable in real DOS mode. Then run `C:\E820.COM` and capture the firmware map. The optical drive may have a different letter on another boot.

The evidence has distinct meanings:

| Observation | What it establishes | What it does not establish |
| --- | --- | --- |
| VirtualBox RAM and `pae=on`; `VBox.log` `EnablePAE=1` | Configured guest RAM and an exposed PAE CPU feature | Win98 setting CR4.PAE or using PAE page tables |
| `Capture` JSON `GuestRegisters` from VirtualBox `debugvm getregisters` | Guest vCPU CR0 paging and CR4.PAE bits at the sampled instant | Whether those bits had other values earlier, or whether high physical frames were used |
| `E820.COM` base, length, type, attributes | Firmware-reported physical address ranges, including ranges above 4 GiB | Win98 VMM accepting, mapping, or using those ranges |
| `memprobe.exe` memory total | Win98 API's reported amount, subject to old 32-bit API limits | Resident backing for all reported RAM |
| `memstress.exe` verified pages | Distinct virtual pages survived write/readback while workers overlapped | Physical residency; the page file can satisfy the test |

PAE **use** requires a sample with CR0.PG and CR4.PAE both set while Win98 runs, plus page-table evidence and VMM page accounting. `Capture` records the guest vCPU registers at the screenshot time, but a sample in DOS or the shutdown BIOS path says nothing about earlier Windows execution. A nominal 4 GiB VM also has a PCI/MMIO address hole, so using all configured RAM may require valid RAM ranges above the 4 GiB address boundary. No current probe reports physical page frame numbers. Do not claim 4 GiB support from a successful VM boot, a PAE checkbox, an E820 map, a saturated memory API value, or page-file-backed `memstress` success. A candidate implementation would need repeatable above-cap physical-frame tests, guest and hypervisor backing evidence, normal shutdown, and no file-system corruption across reboot. No such implementation is present yet.

### 512 MiB control run (2026-09-23)

An independent 512 MiB, PAE-off clone reached the Windows 98 desktop. `GlobalMemoryStatus.total_phys` was `0x1FB80000` (507.5 MiB); `GlobalMemoryStatusEx` was unavailable. `MEMSTRS.EXE 8 1` reported 8 MiB and 2,048 pages verified with one live worker and `ALL_REQUESTED_PAGES_VERIFIED`. After copying the DOS probe to the clone's C: drive, real DOS `E820.COM` reported a type-1 RAM range from `0x00100000` of length `0x1FEF0000`; the BIOS returned 20-byte entries (`attrs=legacy20`). These results establish a working control at this setting, not 4 GiB support.

The subsequent Windows shutdown stayed at its splash screen. VirtualBox recorded no APM shutdown request. Repeated debugger samples found the guest at BIOS `F000:709D`, a self-jump (`EB FD`), with CR0 paging off. A guest-facing ACPI power-button signal had no effect. After retaining screen, log, and CPU evidence, only this disposable clone was forcibly powered off; `finish.json` records `ForcedAfterStall`, and the Base disk SHA-256 remained unchanged. Local evidence is under the ignored `vm/accel/memory-tests/Win98Modern-Mem-512-PAE-Off/` directory. The cause of this shutdown stall has not been isolated to RAM size, the DOS-mode roundtrip, or a guest driver.

### 4 GiB PAE on/off comparison (2026-09-23)

Separate full clones were configured at 4096 MiB with PAE on and off. The autonomous real-mode BIOS probe completed in both. Both E820 maps included a type-1 RAM range at guest physical base `0x0000000100000000` with length `0x0000000020000000` (512 MiB above the 4 GiB boundary). The E820 maps matched across PAE settings. This establishes what the virtual firmware advertises, not what Windows accepts.

Both Windows boots stopped at the same DOS text startup error naming Windows, `CONFIG.SYS`, and `AUTOEXEC.BAT`, before the desktop. Both guests then issued an APM shutdown request and powered off. `MEMPROBE.EXE` and `MEMSTRS.EXE` could not run, so there is **no guest-visible usable-RAM measurement at 4 GiB**. At the captured error screen, VirtualBox reported `CR0=0x10`, `CR3=0`, and `CR4=0`; paging and CR4.PAE were off at that instant. These real-mode samples do not establish whether CR4.PAE changed earlier during the failed Windows initialization. Enabling VirtualBox's PAE flag did not make this unmodified Win98 installation boot with 4 GiB, and no 4 GiB Win98 memory support has been demonstrated. Both clones are powered off; their `finish.json` files confirm the Base disk hash still matches. Local screenshots, logs, and register samples remain in the ignored `vm/accel/memory-tests/Win98Modern-Mem-4096-PAE-On/` and `Win98Modern-Mem-4096-PAE-Off/` folders.

Microsoft [KB253912](https://ftp.zx.net.nz/pub/Patches/ftp.microsoft.com/MISC/KB/en-us/253/912.HTM) describes the VCache address-space failure beyond 512 MiB. [KB304943](https://ftp.zx.net.nz/pub/Patches/ftp.microsoft.com/MISC/KB/en-us/304/943.HTM) says Windows 98 was not designed for more than 1 GiB and `MaxPhysPage=40000` limits use to 1 GiB. Neither adjustment implements PAE or all-RAM use. A genuine solution requires Win9x VMM and memory manager changes, including physical page accounting, cache mapping, PCI/MMIO holes, above-4-GiB frames, and regression testing across CSM systems. Those changes are not yet implemented.
