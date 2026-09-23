# Direct guest validation

`vm/README.md` contains the latest setup evidence and exact media hashes. The original Korean Windows 98 SE OEM ISO stays ignored. Installation was performed in VirtualBox with BIOS/IDE; on this host VirtualBox uses WHPX/NEM hardware virtualization. `vm/run.ps1` checks the current NEM log. Confirm VM identity and power state before changing media or settings; never overwrite `Win98Modern-Base`. Use the test VM or a new full clone for experiments.

On this modern host the original installation hit a VMM TLB issue. The recorded recovery used Patcher9x `-select tlb` on the test clone, not a memory or 4 GiB patch. KernelEx 4.5.2 required Microsoft Layer for Unicode (`UNICOWS.DLL`) first. Keep those third-party installers out of Git and release archives. Back up the installed `core.ini` and change only the required KernelEx `contents` entry; test restoration and reboot.

Build read-only test CDs from explicit media. `tools/make_test_iso.py` is the allowlisted wrapper-test path; `tools/make_app_probe_iso.py` builds a separate extracted-app probe CD. Never confuse host smoke success with guest success. Record screenshots or logs for direct wrapper calls, static KERNEL32 import resolution, app loader messages, startup, and useful app actions. The five mandatory app probes each need evidence of a successful launch and a meaningful function in the installed guest; a resolved import, splash screen, or host run does not satisfy that criterion. Keep screenshots that might contain installation keys ignored and private.

Win98 may show a startup menu after an abnormal previous stop. Choose normal boot deliberately and distinguish a guest APM shutdown from a hypervisor crash using `VMState` and `VBox.log`. Shut down from the guest where possible. The current standard VGA display is limited to 16 colors; presentation faults alone do not prove API failure.

For repeatable remote testing, inspect `remote/README.md` and `remote/PROTOCOL.md`
in the project. The visible COM1 agent, local named-pipe controller and explicit
test suites replace repeated CD deployment once their guest transport passes.
Host protocol tests alone do not prove serial communication. COM1 may need manual
Windows device registration even with the virtual UART enabled. Record any
post-driver boot failure and isolate it; a VxD address alone is not a diagnosis.
The 2026-09-23 post-COM boot exception did not show an absent TLB patch:
Patcher9x reported VMM32 already patched and made no change.

The controller verifies a temporary upload before promoting it and retains the
previous file as a backup. After a lost mutating reply, inspect the recorded
stage/destination/backup paths instead of replaying the request. Keep console
EXEC exit codes, GUI window observations, static KernelEx routing and real app
functionality as separate evidence. The GUI observer is bounded and cleans up
its immediate child; it cannot certify descendants or app functionality.
