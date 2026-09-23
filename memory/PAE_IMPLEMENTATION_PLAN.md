# PAE implementation path for Windows 98 SE

This is an engineering plan, **not an implementation or a claim of 4 GiB support**. The target is the usable physical RAM reported by a tested, CSM-bootable machine, including frames above `0xFFFFFFFF` when the CPU can address them. It does not enlarge any one 32-bit process's virtual address space. Intel's [system programming manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-system-programming-manual-325384.pdf) defines PAE page translation and its control registers; Microsoft's [PAE description](https://learn.microsoft.com/en-us/windows/win32/memory/physical-address-extension) explains the distinction between physical and virtual address space and does **not** list Windows 98 among its PAE-capable Windows releases. Those references describe hardware and other Windows systems, not a ready-made Windows 98 implementation.

## Observed starting point (2026-09-23)

| Test | Verified observation | Limit of the evidence |
| --- | --- | --- |
| Installed Windows 98 SE, 512 MiB, VM PAE off | Disposable full clone reached the GUI. `GlobalMemoryStatus.total_phys=0x1FB80000` (507.5 MiB). An 8 MiB / 2,048-page cross-process virtual-memory readback passed. | The pages could be paged out; the test does not identify physical frame numbers. A later shutdown stalled in a BIOS self-loop, requiring a forced stop of this clone. |
| 4,096 MiB, VM PAE on and off | Both independent clones' firmware E820 maps include type-1 RAM from `0x100000000` for `0x20000000` bytes (512 MiB above 4 GiB). | Both Windows boots stopped at the same DOS text startup error before the GUI. No guest RAM probe or stress test ran. E820 describes firmware availability, not VMM acceptance. |
| Failed 4,096 MiB boot register samples | At the captured text error: `CR0=0x10`, `CR3=0`, `CR4=0` in both clones. Both subsequently made an APM power-off request. | These are real-mode error-screen samples; they neither prove nor rule out a PAE transition at an earlier instant. The VM's `pae=on` setting only exposes a CPU feature. |

The detailed method, local evidence locations and their safety limits are in [README.md](README.md). The repository has a DOS E820 reader and Win32 virtual-memory probes, but **no PAE VMM, high-physical-frame allocator, or Win9x PAE page-table code**. KernelEx's bundled VxD hooks application/system APIs; it is not a replacement memory manager. `MaxPhysPage` or VCache limits can help bound an experiment but cannot fulfill the all-RAM target.

## Minimum implementation sequence and gates

Each gate must pass in a disposable, independent VM clone before the next gate changes paging or exposes high RAM. Keep the original installed VM and ISO intact. Preserve exact source version, VM configuration, firmware map, `VBox.log`, screenshots, guest register samples, and base-disk hash for each run.

### 1. Isolate the existing boot failure and map VMM ownership

Run the existing 512 MiB control again with an ordinary GUI startup and shutdown. Then test 1,024, 2,048 and 3,072 MiB separately with PAE off, increasing only after the lower setting boots and shuts down. Repeat the 4,096 MiB comparison only after the last stable setting is known. Record the *exact* startup message, `SYSTEM.INI` settings, and the stage of failure; determine whether the 4 GiB stop is in DOS initialization, Windows initialization, VMM, VCache, or a driver before attributing it to PAE. Reproduce the 512 MiB shutdown stall independently of the DOS-mode roundtrip. The gate is a repeatable boot/shutdown baseline and a localized failure, not a `MaxPhysPage` workaround presented as capacity.

Separately audit the installed Win98 build's VMM/VMM32 startup, page allocation and free, page fault, swap, cache and mapping paths, and the VxD interfaces that expose physical addresses. Use read-only tracing/instrumentation first to record a **running GUI** `CR0.PG`, `CR3`, `CR4.PAE`, page-table layout, and VMM frame accounting. Record offsets and signatures for the exact installed binary build; never assume a layout from KernelEx, Wine, ReactOS, or another Win9x edition applies. Gate: a documented ownership map and reproducible low-RAM telemetry without changes to guest memory management.

### 2. Build a safe, early 64-bit firmware-memory inventory

Before VMM starts assigning pages, carry the full 64-bit E820 base and length through a normalized, overflow-checked map. Split overlaps and page-align ranges; accept only usable type-1 pages after subtracting firmware, ACPI, PCI/MMIO, boot code, kernel, page tables, DMA and other reservations. Do not assume every advertised byte is free. Reject ranges beyond the processor's supported physical-address width, as reported by CPUID, and report that limitation explicitly. Gate: the guest's counted accepted, reserved, and rejected frames reconcile with the captured E820 map, including the above-4-GiB range. This remains an inventory, not proof that Windows uses the frames.

### 3. Replace or take ownership of the VMM paging path

The project has no Windows 98 VMM source. A working design therefore needs a reproducible, auditable **boot-time integration point** that owns VMM page-table creation and every later update, or a sufficiently complete VMM replacement. A late user-mode DLL or a VxD that only sets `CR4.PAE` cannot do this: 32-bit paging entries and PAE entries have different widths and hierarchies. Keep the integration reversible and distribute project source/patch logic, not Microsoft VMM binaries.

Build 64-bit PAE entries, a four-entry page-directory-pointer table, page directories and page tables for the existing 32-bit linear map. Preserve Win9x mappings and protection semantics for the system, user processes, DOS/virtual-8086 contexts, VxDs, shared pages and page-fault handlers. Audit page-table allocation, reloads, invalidations, large-page use, interrupt/exception entry, and any real-mode or BIOS transitions. Design the paging-mode transition against the Intel manual and test it at low RAM **before** adding high frames. Gate: the 512 MiB installed guest repeatedly boots, runs the existing apps/probes, reboots and shuts down under PAE with `CR0.PG=1`, `CR4.PAE=1`, PAE-format tables and no filesystem corruption. A register bit alone does not pass the gate.

### 4. Extend VMM physical-frame accounting above 4 GiB

Use a physical-frame identifier and range/quantity calculations wide enough for the target address width throughout the allocator, free lists, reference counts, pinned pages, swap, copy-on-write, page cache, and page-fault path. Provide temporary 32-bit virtual mappings to initialize and inspect high frames; a 32-bit pointer is never the physical address. Keep essential bootstrap and compatibility structures in low RAM where required. Make VMM accounting and a debug-only frame-identity probe expose allocated physical frame numbers and whether a page is resident, without exporting arbitrary physical-memory access to user applications. Gate: allocate, modify, verify, free and reallocate frames with physical addresses both below and above 4 GiB while PAE is active. A `VirtualAlloc` readback by itself is insufficient because the page file may back it.

### 5. Audit DMA, MMIO and legacy physical-address consumers

Win9x drivers and DOS-facing paths may pass 32-bit physical addresses or require DMA below a device-specific limit. Classify each producer/consumer of a physical address. Use a low-memory pool or bounce buffers for constrained DMA; do not truncate a high physical address into an old 32-bit interface. Keep PCI/MMIO holes unmapped as RAM and preserve memory types/cache attributes for device mappings. Ensure VCache and the filesystem never hand unsafe high frames to a driver. Gate: disk and filesystem integrity, paging I/O, cache pressure, DOS-box use, reboot and normal shutdown pass with high memory enabled, with traces showing each constrained DMA buffer stayed in range.

### 6. Demonstrate usable capacity, not merely a boot

Test 512 MiB and the staged 1–4 GiB sizes first, then a VM with **more than 4 GiB configured** so the high-frame pool is substantial. Compare VMM `managed + reserved + rejected` frame totals against the captured E820 map, and account explicitly for the PCI/MMIO hole. Pin selected guest pages, record their VMM physical frame identities, exercise high and low frames concurrently across processes, and corroborate the high-frame mappings with hypervisor page-table/backing evidence. Test memory pressure and page-file interactions separately, then reboot and check data and filesystem integrity. Legacy 32-bit `GlobalMemoryStatus` values may saturate; use wide internal telemetry rather than treating them as the authoritative capacity. Report the exact capacity verified for each VM or physical machine, not a general hardware guarantee.

The final CSM hardware gate requires a named, testable machine with its firmware E820 map, CPU physical-address width, PCI/MMIO layout and DMA-capable driver set. No such physical machine is currently available; VM results cannot establish Skylake hardware support. The release criterion is that all **usable, addressable** RAM on each named supported system is either managed by the Win98 memory manager or explicitly excluded for a documented reservation, with repeated high-frame use and normal boot/shutdown. Windows 98 application pointers remain 32-bit throughout.

## Immediate next deliverable

The smallest useful coding step is read-only VMM/boot telemetry at the stable 512 MiB setting, coupled with 1–3 GiB staged clone results. It should answer where the 4 GiB boot fails and which component owns page tables before any CR4.PAE write is attempted. No stage above has yet been implemented in this repository.
