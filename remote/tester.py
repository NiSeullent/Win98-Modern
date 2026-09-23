"""Deploy and execute an explicit test suite in the installed Win98 guest."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time

from controller import Client, DEFAULT_PIPE, NamedPipeTransport

ROOT = Path(__file__).resolve().parents[1]
VBOX = Path(r"C:\Program Files\Oracle\VirtualBox\VBoxManage.exe")


def vm_evidence(name: str, pipe: str) -> dict:
    if name == "Win98Modern-Base" or not name.startswith("Win98Modern-"):
        raise ValueError("use a disposable Win98Modern VM")
    process = subprocess.run([str(VBOX), "showvminfo", name, "--machinereadable"],
                             capture_output=True, text=True, check=True, timeout=30)
    lines = process.stdout.splitlines()
    if 'VMState="running"' not in lines:
        raise RuntimeError("test VM is not running")
    mode = next((line for line in lines if line.startswith("uartmode1=")), "")
    if "server," + pipe not in mode:
        raise RuntimeError("test VM COM1 does not use the requested host pipe")
    folder = ROOT / "vm" / "accel" / name / "Logs"
    log = (folder / "VBox.log").read_text(encoding="utf-8", errors="replace")
    acceleration = [line.strip() for line in log.splitlines()
                    if "NEM: Created partition" in line or "WHvCapabilityCodeHypervisorPresent" in line]
    if not any("NEM: Created partition" in line for line in acceleration):
        raise RuntimeError("current VM log does not prove the required hardware virtualization backend")
    return {"vm": name, "state": "running", "serial": mode,
            "acceleration_log": str(folder / "VBox.log"), "acceleration_evidence": acceleration}


def run_suite(client: Client, suite: dict, report: dict, save) -> None:
    if suite.get("schema") != "m98.remote-suite.v1":
        raise ValueError("unsupported suite schema")
    report["guest"] = client.ping()
    if "os=4.10 " not in report["guest"]:
        raise RuntimeError("agent did not report the required Win98 4.10 guest")
    local_agent = ROOT / "build/m98agent.exe"
    expected_agent = (local_agent.stat().st_size, hashlib.sha256(local_agent.read_bytes()).hexdigest())
    actual_agent = client.remote_hash(r"C:\M98LAB\M98AGENT.EXE")
    if expected_agent != actual_agent:
        raise RuntimeError("guest agent file differs from the local tested build")
    report["agent_sha256"] = actual_agent[1]
    save()
    for item in suite.get("files", []):
        source = (ROOT / item["source"]).resolve(strict=True)
        if not source.is_relative_to(ROOT):
            raise ValueError("suite upload source leaves the repository")
        result = client.put_file(source, item["guest_path"])
        report["transfers"].append(result)
        save()
    for case in suite["tests"]:
        started = time.monotonic()
        result = {"id": case["id"], "passed": False}
        try:
            result.update(client.execute(case["command"], case.get("cwd", r"C:\M98LAB"),
                                         case.get("timeout_ms", 30000)))
            reasons = []
            if result["exit_code"] != case.get("exit_code", 0):
                reasons.append("unexpected exit code")
            if result["timed_out"] != case.get("timed_out", False):
                reasons.append("unexpected timeout state")
            if result["output_truncated"] != case.get("output_truncated", False):
                reasons.append("unexpected output truncation")
            for text in case.get("contains", []):
                if text not in result["output"]:
                    reasons.append("missing output: " + text)
            result["passed"] = not reasons
            result["failures"] = reasons
        except Exception as error:
            result["error"] = str(error)
            report["tests"].append(result)
            save()
            raise  # A broken stream is not reused for later tests.
        finally:
            result["duration_seconds"] = round(time.monotonic() - started, 3)
        report["tests"].append(result)
        save()
        print(("PASS" if result["passed"] else "FAIL") + ": " + case["id"], flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", type=Path)
    parser.add_argument("--pipe", default=DEFAULT_PIPE)
    parser.add_argument("--vm", default="Win98Modern-Accel-128")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    suite = json.loads(args.suite.read_text(encoding="utf-8"))
    stamp = datetime.now(timezone.utc)
    output = args.output or ROOT / "build/remote" / (stamp.strftime("%Y%m%dT%H%M%SZ") + "-results.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    report = {"schema": "m98.guest-results.v1", "started_utc": stamp.isoformat(),
              "suite": str(args.suite.resolve()), "suite_sha256": hashlib.sha256(args.suite.read_bytes()).hexdigest(),
              "transport": "VirtualBox COM1 local named pipe", "pipe": args.pipe,
              "transfers": [], "tests": [], "completed": False, "passed": False}

    def save():
        output.write_text(json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")

    try:
        report.update(vm_evidence(args.vm, args.pipe))
        with NamedPipeTransport(args.pipe) as transport:
            run_suite(Client(transport), suite, report, save)
        report["completed"] = True
        report["passed"] = bool(report["tests"]) and all(case["passed"] for case in report["tests"])
    except Exception as error:
        report["error"] = str(error)
        print("ERROR: " + str(error), flush=True)
    finally:
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        save()
    print("Evidence: " + str(output), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
