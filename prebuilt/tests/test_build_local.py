"""Small synthetic VirtualBox fixtures; no guest or VM mutation."""

import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_local as pb


SOURCE_UUID = "11111111-1111-1111-1111-111111111111"
DISK_UUID = "22222222-2222-2222-2222-222222222222"
SNAP_UUID = "33333333-3333-3333-3333-333333333333"
SNAP_NAME = "stable-checkpoint"
CLONE_UUID = "55555555-5555-5555-5555-555555555555"
CLONE_DISK_UUID = "66666666-6666-6666-6666-666666666666"


def hardware(disk_uuid=DISK_UUID):
    return f'''<Hardware>
      <Firmware/><StorageControllers>
      <StorageController name="IDE" type="PIIX4">
        <AttachedDevice type="HardDisk" port="0" device="0"><Image uuid="{{{disk_uuid}}}"/></AttachedDevice>
        <AttachedDevice type="DVD" port="1" device="0"><Image uuid="{{44444444-4444-4444-4444-444444444444}}"/></AttachedDevice>
      </StorageController></StorageControllers></Hardware>'''


def config_xml(*, snapshot=False, saved=False, missing_parent=False):
    if missing_parent:
        media = f'<HardDisk uuid="{{{DISK_UUID}}}" location="system.vdi"/>'
    else:
        media = f'<HardDisk uuid="{{{DISK_UUID}}}" location="system.vdi"/>'
    snap = (f'<Snapshot uuid="{{{SNAP_UUID}}}" name="{SNAP_NAME}"'
            + (' stateFile="Snapshots/state.sav"' if saved else '')
            + f'>{hardware()}</Snapshot>') if snapshot else ''
    return (f'<VirtualBox><Machine uuid="{{{SOURCE_UUID}}}"><MediaRegistry>'
            f'<HardDisks>{media}</HardDisks></MediaRegistry>{snap}{hardware()}</Machine></VirtualBox>')


class FakeVBox:
    def __init__(self, config, *, state="poweroff", snapshot=False, host_virtualization=True):
        self.config = config
        self.state = state
        self.snapshot = snapshot
        self.host_virtualization = host_virtualization
        self.calls = []

    def run(self, *args, **_kwargs):
        self.calls.append(args)
        if args == ("list", "hostinfo"):
            return "Processor supports HW virtualization: " + ("yes" if self.host_virtualization else "no") + "\n"
        if args == ("list", "vms"):
            return f'"Source" {{{SOURCE_UUID}}}\n'
        if args[:2] == ("showvminfo", SOURCE_UUID):
            # JSON escapes mimic VBoxManage's machine-readable path value.
            import json
            return (f'VMState="{self.state}"\nfirmware="BIOS"\nchipset="piix3"\n'
                    f'hwvirtex="on"\nCfgFile={json.dumps(str(self.config))}\n')
        if args == ("snapshot", SOURCE_UUID, "list", "--machinereadable") and self.snapshot:
            return f'SnapshotName="{SNAP_NAME}"\nSnapshotUUID="{SNAP_UUID}"\n'
        if args == ("showmediuminfo", "disk", DISK_UUID):
            return "Capacity:       1 MBytes\nParent UUID:    base\n"
        raise AssertionError("Unexpected VBoxManage command: " + repr(args))


class FakeBuildVBox(FakeVBox):
    def __init__(self, config, out):
        super().__init__(config)
        self.out = out
        self.clone_dir = out / "Clone"
        self.clone_config = self.clone_dir / "Clone.vbox"
        self.clone_disk = self.clone_dir / "Clone.vdi"
        self.registered = False

    def run(self, *args, **kwargs):
        if args[0] == "clonevm":
            self.calls.append(args)
            self.clone_dir.mkdir(parents=True)
            self.clone_disk.write_bytes((self.config.parent / "system.vdi").read_bytes())
            self.clone_config.write_text(
                f'<VirtualBox><Machine uuid="{{{CLONE_UUID}}}"><MediaRegistry><HardDisks>'
                f'<HardDisk uuid="{{{CLONE_DISK_UUID}}}" location="Clone.vdi"/>'
                f'</HardDisks></MediaRegistry>{hardware(CLONE_DISK_UUID)}</Machine></VirtualBox>',
                encoding="utf-8",
            )
            self.registered = True
            return ""
        if args == ("list", "vms") and self.registered:
            self.calls.append(args)
            return f'"Source" {{{SOURCE_UUID}}}\n"Clone" {{{CLONE_UUID}}}\n'
        if args[:2] == ("showvminfo", CLONE_UUID):
            self.calls.append(args)
            return (f'VMState="poweroff"\nfirmware="BIOS"\nchipset="piix3"\n'
                    f'hwvirtex="on"\nstoragecontrollertype0="PIIX4"\n'
                    f'CfgFile={json.dumps(str(self.clone_config))}\n')
        if args[:2] == ("storageattach", CLONE_UUID):
            self.calls.append(args)
            tree = ET.parse(self.clone_config)
            for device in tree.getroot().iter("AttachedDevice"):
                if device.get("type") == "DVD":
                    image = device.find("Image")
                    if image is not None:
                        device.remove(image)
            tree.write(self.clone_config, encoding="unicode")
            return ""
        if args == ("showmediuminfo", "disk", CLONE_DISK_UUID):
            self.calls.append(args)
            return "Capacity:       1 MBytes\nParent UUID:    base\n"
        if args == ("--version",):
            self.calls.append(args)
            return "7.2.18r175117\n"
        return super().run(*args, **kwargs)


class PrebuiltTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.config = self.source / "Source.vbox"
        (self.source / "system.vdi").write_bytes(b"synthetic guest disk; no Microsoft data")
        self.out = self.root / "new-output"

    def write_config(self, **kwargs):
        self.config.write_text(config_xml(**kwargs), encoding="utf-8")

    def test_parser_preserves_windows_path(self):
        info = pb.parse_machine(r'CfgFile="C:\\Lab\\Source.vbox"' + "\nVMState=\"poweroff\"\n")
        self.assertEqual(info["CfgFile"], r"C:\Lab\Source.vbox")
        self.assertEqual(info["VMState"], "poweroff")
        mode = pb.parse_machine('uartmode1="server,\\\\.\\pipe\\Lab"\nVideoMode="640,480,0"@0,0 1\n')
        self.assertIn("pipe", mode["uartmode1"])
        self.assertIn("@0,0 1", mode["VideoMode"])

    def test_powered_off_dry_run_is_read_only(self):
        self.write_config()
        fake = FakeVBox(self.config)
        plan = pb.preflight(fake, "Source", None, "Clone", self.out)
        self.assertEqual(plan["disks"][0].sha256, pb.sha256(self.source / "system.vdi"))
        self.assertFalse(self.out.exists())
        self.assertNotIn("clonevm", [call[0] for call in fake.calls])

    def test_running_current_state_rejected(self):
        self.write_config()
        with self.assertRaisesRegex(pb.PrebuiltError, "powered off"):
            pb.preflight(FakeVBox(self.config, state="running"), "Source", None, "Clone", self.out)

    def test_named_immutable_snapshot_of_running_vm_allowed(self):
        self.write_config(snapshot=True)
        plan = pb.preflight(FakeVBox(self.config, state="running", snapshot=True),
                            "Source", SNAP_NAME, "Clone", self.out)
        self.assertEqual(plan["snapshot"], (SNAP_NAME, SNAP_UUID))
        self.assertFalse(self.out.exists())

    def test_saved_state_snapshot_rejected(self):
        self.write_config(snapshot=True, saved=True)
        with self.assertRaisesRegex(pb.PrebuiltError, "saved/running"):
            pb.preflight(FakeVBox(self.config, state="running", snapshot=True),
                         "Source", SNAP_NAME, "Clone", self.out)

    def test_host_without_virtualization_rejected(self):
        self.write_config()
        with self.assertRaisesRegex(pb.PrebuiltError, "VT-x/AMD-V"):
            pb.preflight(FakeVBox(self.config, host_virtualization=False),
                         "Source", None, "Clone", self.out)

    def test_existing_destination_rejected(self):
        self.write_config()
        (self.out / "Clone").mkdir(parents=True)
        with self.assertRaisesRegex(pb.PrebuiltError, "already exists"):
            pb.preflight(FakeVBox(self.config), "Source", None, "Clone", self.out)

    def test_key_shaped_name_rejected(self):
        self.write_config()
        with self.assertRaisesRegex(pb.PrebuiltError, "product key"):
            pb.preflight(FakeVBox(self.config), "AAAAA-BBBBB-CCCCC-DDDDD-EEEEE",
                         None, "Clone", self.out)

    def test_source_and_destination_may_not_overlap(self):
        self.write_config()
        with self.assertRaisesRegex(pb.PrebuiltError, "overlap"):
            pb.preflight(FakeVBox(self.config), "Source", None, "Clone", self.source / "child")

    def test_removable_media_is_identified_for_clone_only(self):
        self.write_config()
        _, machine = pb.read_vbox(self.config)
        hw = pb.selected_hardware(machine, None)
        calls = []

        class Recorder:
            def run(self, *args):
                calls.append(args)

        pb.empty_removable(Recorder(), "CLONE-UUID", hw)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][0:3], ("storageattach", "CLONE-UUID", "--storagectl=IDE"))
        self.assertIn("--medium=emptydrive", calls[0])

    def test_build_writes_hash_manifest_for_independent_clone(self):
        self.write_config()
        original = (self.source / "system.vdi").read_bytes()
        fake = FakeBuildVBox(self.config, self.out)
        plan = pb.preflight(fake, "Source", None, "Clone", self.out)
        manifest_path = pb.build(fake, plan)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["source"]["vm_uuid"], SOURCE_UUID)
        self.assertEqual(manifest["clone"]["vm_uuid"], CLONE_UUID)
        self.assertEqual(manifest["clone"]["disk"]["sha256"], pb.sha256(fake.clone_disk))
        self.assertEqual(manifest["source"]["disk_chain_leaf_first"][0]["sha256"],
                         pb.sha256(self.source / "system.vdi"))
        self.assertFalse(manifest["runtime_acceleration_verified"])
        self.assertEqual((self.source / "system.vdi").read_bytes(), original)
        self.assertEqual(len([c for c in fake.calls if c[0] == "clonevm"]), 1)
        self.assertNotIn("product_key", manifest_path.read_text(encoding="utf-8"))

    def test_clone_may_remove_only_transient_guest_properties(self):
        self.write_config()
        marker = ('<GuestProperties><GuestProperty name="/VirtualBox/HostInfo/VBoxVer" '
                  'value="7.2.18" flags="TRANSIENT, RDONLYGUEST"/></GuestProperties>')
        self.config.write_text(self.config.read_text(encoding="utf-8").replace(
            '</Machine>', marker + '</Machine>'), encoding="utf-8")

        class TransientRewriter(FakeBuildVBox):
            def run(self, *args, **kwargs):
                result = super().run(*args, **kwargs)
                if args[0] == 'clonevm':
                    self.config.write_text(self.config.read_text(encoding="utf-8").replace(
                        marker, ''), encoding="utf-8")
                return result

        fake = TransientRewriter(self.config, self.out)
        plan = pb.preflight(fake, "Source", None, "Clone", self.out)
        manifest = json.loads(pb.build(fake, plan).read_text(encoding="utf-8"))
        config = manifest['source']['configuration']
        self.assertNotEqual(config['sha256_at_preflight'], config['sha256_at_completion'])
        self.assertEqual(config['stable_sha256_at_preflight'],
                         pb.stable_config_sha256(self.config))

    def test_clone_rejects_material_source_configuration_change(self):
        self.write_config()

        class HardwareRewriter(FakeBuildVBox):
            def run(self, *args, **kwargs):
                result = super().run(*args, **kwargs)
                if args[0] == 'clonevm':
                    self.config.write_text(self.config.read_text(encoding="utf-8").replace(
                        '<Firmware/>', '<Firmware type="EFI"/>'), encoding="utf-8")
                return result

        fake = HardwareRewriter(self.config, self.out)
        plan = pb.preflight(fake, "Source", None, "Clone", self.out)
        with self.assertRaisesRegex(pb.PrebuiltError, 'source VM changed'):
            pb.build(fake, plan)
        self.assertFalse((fake.clone_dir / 'prebuilt-manifest.json').exists())

    def test_clone_serial_pipe_uses_independent_endpoint(self):
        calls = []

        class Recorder:
            def run(self, *args):
                calls.append(args)

        pipes = pb.isolate_serial(Recorder(), CLONE_UUID, "Clone",
                                  {"uart1": "0x03f8,4", "uartmode1": r"server,\\.\pipe\Source"})
        self.assertEqual(len(pipes), 1)
        self.assertIn("Clone-55555555-com1", pipes[0])
        self.assertNotIn("Source", pipes[0])
        self.assertEqual(calls[0][0:3], ("modifyvm", CLONE_UUID, "--uart-mode1"))


if __name__ == "__main__":
    unittest.main()
