#!/usr/bin/env python3
"""Create a local, independently cloned VirtualBox Windows 98 VM.

The default operation is a read-only preflight. The explicit --build operation
never starts either guest and writes only inside the selected local output tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parent
LOCAL = HERE / "local"
KEY_SHAPE = re.compile(r"(?i)(?<![a-z0-9])[a-z0-9]{5}(?:-[a-z0-9]{5}){4}(?![a-z0-9])")
CLONE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}\Z")
VM_LINE = re.compile(r'^"(.*)" \{([0-9a-fA-F-]{36})\}$')


class PrebuiltError(Exception):
    """Expected, user-facing preflight or validation failure."""


def safe_text(value: str) -> str:
    if KEY_SHAPE.search(value):
        raise PrebuiltError("A VM name or path resembles a product key; rename it before creating a manifest")
    return value


def inside(path: Path, parent: Path) -> bool:
    child = os.path.normcase(str(path.resolve()))
    base = os.path.normcase(str(parent.resolve()))
    return child == base or child.startswith(base + os.sep)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def stable_config_sha256(path: Path) -> str:
    """Hash VM configuration after excluding VirtualBox runtime guest metadata.

    ``clonevm`` can remove TRANSIENT, RDONLYGUEST HostInfo/VMInfo properties
    from the powered-off source .vbox without changing its hardware or disk.
    All other XML, including non-transient guest properties, remains checked.
    """
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as exc:
        raise PrebuiltError("The source VM configuration cannot be read") from exc
    for parent in root.iter():
        for node in list(parent):
            if node.tag.rsplit("}", 1)[-1] != "GuestProperties":
                continue
            for prop in list(node):
                flags = {flag.strip().upper() for flag in prop.get("flags", "").split(",")}
                if (prop.tag.rsplit("}", 1)[-1] == "GuestProperty"
                        and {"TRANSIENT", "RDONLYGUEST"} <= flags):
                    node.remove(prop)
            if len(node) == 0 and not node.attrib:
                parent.remove(node)
    return hashlib.sha256(ET.tostring(root, encoding="utf-8")).hexdigest()


def parse_machine(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if value.startswith('"'):
            try:
                value = json.loads(value)
            except json.JSONDecodeError:
                # VBoxManage calls this machine-readable, but some values
                # (for example a UART pipe or video mode) use literal Windows
                # backslashes that are not valid JSON escapes. Preserve those
                # values; the fields used for paths are still unescaped.
                if value.endswith('"'):
                    value = value[1:-1].replace('\\\\', '\\').replace('\\"', '"')
        result[key] = value
    return result


def xml_child(parent: ET.Element, name: str) -> ET.Element | None:
    return parent.find("{*}" + name)


def xml_children(parent: ET.Element, name: str) -> list[ET.Element]:
    return list(parent.findall("{*}" + name))


class VBox:
    def __init__(self, executable: Path):
        if not executable.is_file():
            raise PrebuiltError("VBoxManage was not found; pass --vboxmanage")
        self.executable = executable

    def run(self, *args: str, timeout: int = 120) -> str:
        try:
            result = subprocess.run(
                [str(self.executable), *args], capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=timeout, check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise PrebuiltError(f"VBoxManage {args[0]} could not complete") from exc
        if result.returncode:
            # VBoxManage output is deliberately not echoed: VM names, paths and
            # guest descriptions can contain private information.
            raise PrebuiltError(f"VBoxManage {args[0]} failed (exit {result.returncode})")
        return result.stdout


@dataclass(frozen=True)
class Disk:
    uuid: str
    path: Path
    size: int
    sha256: str

    def manifest(self) -> dict:
        return {"uuid": self.uuid, "path": str(self.path), "bytes": self.size, "sha256": self.sha256}


def read_vbox(path: Path) -> tuple[ET.Element, ET.Element]:
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as exc:
        raise PrebuiltError("The VM configuration file cannot be read") from exc
    machine = xml_child(root, "Machine")
    if machine is None:
        raise PrebuiltError("The VM configuration has no Machine element")
    return root, machine


def snapshot_uuid(vbox: VBox, source_uuid: str, name: str) -> str:
    lines = parse_machine(vbox.run("snapshot", source_uuid, "list", "--machinereadable"))
    matches = []
    for key, value in lines.items():
        if key == "SnapshotName" or key.startswith("SnapshotName-"):
            if value == name:
                suffix = key[len("SnapshotName"):]
                uid = lines.get("SnapshotUUID" + suffix)
                if uid:
                    matches.append(uid)
    if len(matches) != 1:
        raise PrebuiltError("The named snapshot must exist exactly once")
    return matches[0].lower()


def selected_hardware(machine: ET.Element, snapshot: tuple[str, str] | None) -> ET.Element:
    selected = machine
    if snapshot:
        name, uid = snapshot
        found = [node for node in machine.iter() if node.tag.rsplit("}", 1)[-1] == "Snapshot"
                 and node.get("uuid", "").strip("{}").lower() == uid]
        if len(found) != 1 or found[0].get("name") != name:
            raise PrebuiltError("The named snapshot does not match the VM configuration")
        selected = found[0]
        if selected.get("stateFile"):
            raise PrebuiltError("Snapshots with a saved/running state are not supported")
    hardware = xml_child(selected, "Hardware")
    if hardware is None:
        raise PrebuiltError("The selected VM state has no hardware settings")
    firmware = xml_child(hardware, "Firmware")
    if firmware is not None and firmware.get("type", "BIOS").upper() != "BIOS":
        raise PrebuiltError("The selected VM state is not BIOS firmware")
    cpu = xml_child(hardware, "CPU")
    hwvirt = xml_child(cpu, "HardwareVirtEx") if cpu is not None else None
    if hwvirt is not None and hwvirt.get("enabled", "true").lower() == "false":
        raise PrebuiltError("The selected VM state disables VT-x/AMD-V")
    controllers = xml_child(hardware, "StorageControllers")
    if controllers is None:
        raise PrebuiltError("The selected VM state has no storage controllers")
    ide = [c for c in xml_children(controllers, "StorageController")
           if c.get("name") == "IDE" and c.get("type") == "PIIX4"]
    if len(ide) != 1:
        raise PrebuiltError("A PIIX4 IDE controller is required")
    disks = [d for d in xml_children(ide[0], "AttachedDevice")
             if d.get("type") == "HardDisk"]
    if len(disks) != 1 or disks[0].get("port") != "0" or disks[0].get("device") != "0":
        raise PrebuiltError("Exactly one IDE disk at port 0/device 0 is required")
    return hardware


def selected_disk_uuid(hardware: ET.Element) -> str:
    controllers = xml_child(hardware, "StorageControllers")
    assert controllers is not None
    for controller in xml_children(controllers, "StorageController"):
        if controller.get("name") != "IDE":
            continue
        for device in xml_children(controller, "AttachedDevice"):
            if device.get("type") == "HardDisk":
                image = xml_child(device, "Image")
                if image is not None:
                    return image.attrib["uuid"].strip("{}").lower()
    raise PrebuiltError("The IDE disk has no image")


def disk_chain(machine: ET.Element, config: Path, leaf_uuid: str) -> list[Disk]:
    registry = xml_child(machine, "MediaRegistry")
    harddisks = xml_child(registry, "HardDisks") if registry is not None else None
    if harddisks is None:
        raise PrebuiltError("No hard disk media registry was found")
    media: dict[str, tuple[Path, str | None]] = {}

    def visit(node: ET.Element, parent: str | None) -> None:
        uid = node.attrib["uuid"].strip("{}").lower()
        location = Path(node.attrib["location"])
        path = (location if location.is_absolute() else config.parent / location).resolve()
        media[uid] = (path, parent)
        for child in xml_children(node, "HardDisk"):
            visit(child, uid)

    for root_disk in xml_children(harddisks, "HardDisk"):
        visit(root_disk, None)
    chain: list[Disk] = []
    seen: set[str] = set()
    uid: str | None = leaf_uuid
    while uid:
        if uid in seen or uid not in media:
            raise PrebuiltError("The selected disk has a missing or cyclic parent")
        seen.add(uid)
        path, parent = media[uid]
        safe_text(str(path))
        if not path.is_file():
            raise PrebuiltError("A source disk in the chain is missing")
        chain.append(Disk(uid, path, path.stat().st_size, sha256(path)))
        uid = parent
    return chain


def empty_removable(vbox: VBox, clone_uuid: str, hardware: ET.Element) -> None:
    controllers = xml_child(hardware, "StorageControllers")
    assert controllers is not None
    for controller in xml_children(controllers, "StorageController"):
        name = controller.get("name", "")
        for device in xml_children(controller, "AttachedDevice"):
            kind = device.get("type")
            if kind not in ("DVD", "Floppy") or xml_child(device, "Image") is None:
                continue
            vbox.run("storageattach", clone_uuid, "--storagectl=" + name,
                     "--port=" + device.attrib["port"], "--device=" + device.attrib["device"],
                     "--type=" + ("dvddrive" if kind == "DVD" else "fdd"),
                     "--medium=emptydrive")


def assert_no_removable_media(hardware: ET.Element) -> None:
    controllers = xml_child(hardware, "StorageControllers")
    assert controllers is not None
    for controller in xml_children(controllers, "StorageController"):
        for device in xml_children(controller, "AttachedDevice"):
            if device.get("type") in ("DVD", "Floppy") and xml_child(device, "Image") is not None:
                raise PrebuiltError("The clone still has removable installation media mounted")


def isolate_serial(vbox: VBox, clone_uuid: str, clone_name: str,
                   info: dict[str, str]) -> list[str]:
    """Keep COM ports useful without cloning the source host's pipe endpoint."""
    pipes = []
    for number in range(1, 5):
        if info.get("uart" + str(number), "off") == "off":
            continue
        mode = info.get("uartmode" + str(number), "")
        option = "--uart-mode" + str(number)
        if mode.startswith("server,"):
            pipe = "\\\\.\\pipe\\" + clone_name + "-" + clone_uuid[:8] + "-com" + str(number)
            vbox.run("modifyvm", clone_uuid, option, "server", pipe)
            pipes.append(pipe)
        elif mode and mode != "disconnected":
            # Client, TCP, physical COM and file endpoints may point back to
            # source resources. Leave the guest COM device but detach its host
            # endpoint. Users can explicitly configure a new one later.
            vbox.run("modifyvm", clone_uuid, option, "disconnected")
    return pipes


def vms(vbox: VBox) -> dict[str, str]:
    result = {}
    for line in vbox.run("list", "vms").splitlines():
        match = VM_LINE.match(line)
        if match:
            result[match.group(1)] = match.group(2).lower()
    return result


def default_vboxmanage() -> Path:
    located = shutil.which("VBoxManage")
    if located:
        return Path(located)
    return Path(os.environ.get("PROGRAMFILES", "C:/Program Files")) / "Oracle" / "VirtualBox" / "VBoxManage.exe"


def preflight(vbox: VBox, source: str, snapshot_name: str | None, clone_name: str,
              output_root: Path) -> dict:
    safe_text(source)
    if snapshot_name:
        safe_text(snapshot_name)
    safe_text(str(output_root))
    if not CLONE_NAME.fullmatch(clone_name):
        raise PrebuiltError("Clone name must use only letters, digits, dot, underscore or hyphen")
    if inside(output_root, REPO) and not inside(output_root, LOCAL):
        raise PrebuiltError("Output inside the repository is allowed only under prebuilt/local")
    host = vbox.run("list", "hostinfo")
    if not re.search(r"^Processor supports HW virtualization:\s+yes\s*$", host, re.MULTILINE | re.IGNORECASE):
        raise PrebuiltError("The host does not report VT-x/AMD-V hardware virtualization support")
    known = vms(vbox)
    if source not in known:
        raise PrebuiltError("The source VM is not registered in VirtualBox")
    if clone_name in known:
        raise PrebuiltError("The clone VM name is already registered")
    clone_dir = output_root / clone_name
    if clone_dir.exists():
        raise PrebuiltError("The clone destination already exists")
    info = parse_machine(vbox.run("showvminfo", known[source], "--machinereadable"))
    state = info.get("VMState")
    if not snapshot_name and state != "poweroff":
        raise PrebuiltError("The source VM must be powered off unless a named snapshot is selected")
    if info.get("firmware", "").upper() != "BIOS" or info.get("chipset", "").lower() != "piix3":
        raise PrebuiltError("The source must use BIOS firmware and the PIIX3 chipset")
    if info.get("hwvirtex") != "on":
        raise PrebuiltError("The source must require VT-x/AMD-V hardware acceleration")
    config = Path(info.get("CfgFile", "")).resolve()
    safe_text(str(config))
    if not config.is_file():
        raise PrebuiltError("The source VM configuration file is missing")
    if inside(output_root, config.parent) or inside(config.parent, output_root):
        raise PrebuiltError("The output and source VM directories must not overlap")
    _, machine = read_vbox(config)
    snapshot = None
    if snapshot_name:
        snapshot = (snapshot_name, snapshot_uuid(vbox, known[source], snapshot_name))
    hardware = selected_hardware(machine, snapshot)
    chain = disk_chain(machine, config, selected_disk_uuid(hardware))
    medium = vbox.run("showmediuminfo", "disk", chain[0].uuid)
    capacity = re.search(r"^Capacity:\s+(\d+)\s+MBytes", medium, re.MULTILINE)
    if not capacity:
        raise PrebuiltError("VirtualBox did not report the source disk capacity")
    ancestor = output_root
    while not ancestor.exists():
        ancestor = ancestor.parent
    free_bytes = shutil.disk_usage(ancestor).free
    minimum = max(int(capacity.group(1)) * 1024 * 1024,
                  2 * sum(d.size for d in chain) + 512 * 1024 * 1024)
    if free_bytes < minimum:
        raise PrebuiltError("Insufficient free space for a full independent clone")
    return {"source_name": source, "source_uuid": known[source], "source_state": state,
            "snapshot": snapshot, "config": config, "config_sha256": sha256(config),
            "config_stable_sha256": stable_config_sha256(config),
            "disks": chain, "clone_name": clone_name, "clone_dir": clone_dir,
            "output_root": output_root, "required_free_bytes": minimum,
            "available_free_bytes": free_bytes}


def build(vbox: VBox, plan: dict) -> Path:
    source_uuid = plan["source_uuid"]
    args = ["clonevm", source_uuid, "--name=" + plan["clone_name"],
            "--basefolder=" + str(plan["output_root"]), "--mode=machine", "--register"]
    if plan["snapshot"]:
        # The explicit UUID fixes which immutable snapshot is copied. No Link
        # option is used, so VirtualBox creates a full independent disk.
        args.append("--snapshot=" + plan["snapshot"][1])
    vbox.run(*args, timeout=3600)
    known = vms(vbox)
    clone_uuid = known.get(plan["clone_name"])
    if not clone_uuid or clone_uuid == source_uuid:
        raise PrebuiltError("The new VM was not registered with an independent UUID")
    clone_info = parse_machine(vbox.run("showvminfo", clone_uuid, "--machinereadable"))
    clone_config = Path(clone_info.get("CfgFile", "")).resolve()
    if not inside(clone_config, plan["clone_dir"]):
        raise PrebuiltError("The new VM configuration is outside the selected clone directory")
    _, clone_machine = read_vbox(clone_config)
    clone_hardware = selected_hardware(clone_machine, None)
    empty_removable(vbox, clone_uuid, clone_hardware)
    serial_pipes = isolate_serial(vbox, clone_uuid, plan["clone_name"], clone_info)
    clone_info = parse_machine(vbox.run("showvminfo", clone_uuid, "--machinereadable"))
    _, clone_machine = read_vbox(clone_config)
    clone_hardware = selected_hardware(clone_machine, None)
    assert_no_removable_media(clone_hardware)
    if (clone_info.get("VMState") != "poweroff" or clone_info.get("firmware", "").upper() != "BIOS"
            or clone_info.get("chipset", "").lower() != "piix3" or clone_info.get("hwvirtex") != "on"
            or clone_info.get("storagecontrollertype0") != "PIIX4"):
        raise PrebuiltError("The clone failed BIOS, IDE, power or VT-x/AMD-V checks")
    if xml_child(clone_machine, "Snapshots") is not None:
        raise PrebuiltError("The clone unexpectedly contains snapshot dependencies")
    clone_disks = disk_chain(clone_machine, clone_config, selected_disk_uuid(clone_hardware))
    if len(clone_disks) != 1 or not inside(clone_disks[0].path, plan["clone_dir"]):
        raise PrebuiltError("The clone disk is linked to a parent or outside the clone directory")
    medium = vbox.run("showmediuminfo", "disk", clone_disks[0].uuid)
    if not re.search(r"^Parent UUID:\s+base\s*$", medium, re.MULTILINE):
        raise PrebuiltError("The cloned disk is not a full independent base medium")
    for disk in plan["disks"]:
        if sha256(disk.path) != disk.sha256:
            raise PrebuiltError("A source disk changed during cloning")
    source_info = parse_machine(vbox.run("showvminfo", source_uuid, "--machinereadable"))
    if plan["snapshot"]:
        if snapshot_uuid(vbox, source_uuid, plan["snapshot"][0]) != plan["snapshot"][1]:
            raise PrebuiltError("The source snapshot changed during cloning")
    elif source_info.get("VMState") != "poweroff":
        raise PrebuiltError("The powered-off source VM changed during cloning")
    source_config_final_sha256 = sha256(plan["config"])
    if (not plan["snapshot"] and source_config_final_sha256 != plan["config_sha256"]
            and stable_config_sha256(plan["config"]) != plan["config_stable_sha256"]):
        raise PrebuiltError("The powered-off source VM changed during cloning")
    manifest = {
        "schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "local-only; do not redistribute Microsoft binaries",
        "virtualbox_version": vbox.run("--version").strip(),
        "source": {"vm_name": plan["source_name"], "vm_uuid": source_uuid,
                   "source_state_at_preflight": plan["source_state"],
                   "snapshot": ({"name": plan["snapshot"][0], "uuid": plan["snapshot"][1]}
                                if plan["snapshot"] else None),
                   "configuration": {"path": str(plan["config"]),
                                     "sha256_at_preflight": plan["config_sha256"],
                                     "sha256_at_completion": source_config_final_sha256,
                                     "stable_sha256_at_preflight": plan["config_stable_sha256"]},
                   "disk_chain_leaf_first": [d.manifest() for d in plan["disks"]]},
        "clone": {"vm_name": plan["clone_name"], "vm_uuid": clone_uuid,
                  "configuration": {"path": str(clone_config), "sha256": sha256(clone_config)},
                  "disk": clone_disks[0].manifest(),
                  "firmware": "BIOS", "chipset": "PIIX3", "disk_controller": "IDE/PIIX4",
                  "hwvirtex": "on", "removable_media": "empty", "serial_pipes": serial_pipes},
        "runtime_acceleration_verified": False,
    }
    out = plan["clone_dir"] / "prebuilt-manifest.json"
    if out.exists():
        raise PrebuiltError("The manifest already exists")
    temporary = out.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(out)
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default="Win98Modern-Base", help="registered source VM name")
    parser.add_argument("--snapshot", help="named, powered-off-state snapshot to clone")
    parser.add_argument("--clone-name", default="Win98-Shizuku-SE-Local")
    parser.add_argument("--output-root", type=Path, default=LOCAL)
    parser.add_argument("--vboxmanage", type=Path, default=default_vboxmanage())
    parser.add_argument("--build", action="store_true", help="perform full clone after preflight")
    args = parser.parse_args(argv)
    try:
        vbox = VBox(args.vboxmanage)
        plan = preflight(vbox, args.source, args.snapshot, args.clone_name,
                         args.output_root.resolve())
        if args.build:
            manifest = build(vbox, plan)
            print("BUILD PASS: independent local VM created; manifest: " + str(manifest))
        else:
            print("DRY RUN PASS: source and selected state are safe to clone")
            print("Source UUID: " + plan["source_uuid"])
            print("Selected state: " + ("snapshot " + plan["snapshot"][1]
                                        if plan["snapshot"] else "powered off current state"))
            print("Disk chain: " + str(len(plan["disks"])) + " image(s), SHA-256 recorded in memory")
            print("Full-clone free-space gate: " + str(plan["required_free_bytes"]) + " bytes required")
            print("No VM, disk or manifest was created")
        return 0
    except PrebuiltError as exc:
        print("PREBUILT ERROR: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
