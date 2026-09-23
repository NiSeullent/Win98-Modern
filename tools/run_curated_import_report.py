"""Reproduce the v1 selected-app static import-name report from local media.

The downloaded proprietary app binaries live in ignored benchmarks/media/.
Only generated names, hashes, and counts are saved in benchmarks/.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from measure_pe_coverage import main


ROOT = Path(__file__).resolve().parent.parent
MEDIA = ROOT / "benchmarks" / "media"

TARGETS = {
    "Chromium 150": [
        "chromium-150-r1639845/chrome-win/chrome.exe",
        "chromium-150-r1639845/chrome-win/chrome.dll",
        "chromium-150-r1639845/chrome-win/chrome_elf.dll",
    ],
    "Supermium 144 R5": [
        "supermium-144-r5/Supermium/chrome.exe",
        "supermium-144-r5/Supermium/144.0.7559.256/chrome.dll",
        "supermium-144-r5/Supermium/144.0.7559.256/chrome_elf.dll",
    ],
    "VLC 3.0.24": [
        "vlc-3.0.24/vlc-3.0.24/vlc.exe",
        "vlc-3.0.24/vlc-3.0.24/libvlc.dll",
        "vlc-3.0.24/vlc-3.0.24/libvlccore.dll",
    ],
    "Notepad++ 8.9.8": ["npp-8.9.8/notepad++.exe"],
}

PEERS = {
    "Chromium 150": [
        "chromium-150-r1639845/chrome-win/chrome_elf.dll",
    ],
    "Supermium 144 R5": [
        "supermium-144-r5/Supermium/144.0.7559.256/chrome_elf.dll",
        *(f"supermium-144-r5/Supermium/{name}.dll" for name in (
            "p_advp32", "p_cry32", "p_cryptp", "p_dnsa", "p_dwma",
            "p_evapi", "p_iphlpa", "p_ntd", "p_ole", "p_powrpf",
            "p_s232", "p_setapi", "p_user", "p_usren", "p_vcrt",
            "p_whttp", "pwrp_k32")),
    ],
    "VLC 3.0.24": [
        "vlc-3.0.24/vlc-3.0.24/libvlc.dll",
        "vlc-3.0.24/vlc-3.0.24/libvlccore.dll",
    ],
}

# These are the project routes indexed by tools/index_api_sources.py. Keep
# their source bytes pinned in the report rather than scanning unrelated src.
WRAPPER_SOURCES = (
    "src/m98wrap.c",
    "src/m98advapi.c",
    "src/m98shell.c",
    "src/m98gdi.c",
    "src/m98user.c",
    "src/m98ctl.c",
    "src/bcrypt_shim.def",
    "src/dbghelp_shim.def",
    "src/dwmapi_shim.def",
    "src/uxtheme_shim.def",
)


def run() -> int:
    argparse.ArgumentParser(
        description="Rebuild the pinned selected-app PE import-name report from benchmarks/media."
    ).parse_args()
    args = [
        "report",
        "--baseline", str(ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"),
        "--kernelex-source-manifest",
        str(ROOT / "benchmarks/kernelex-source-declarations-v1.json"),
    ]
    for source in WRAPPER_SOURCES:
        args += ["--wrapper-source", str(ROOT / source)]
    for app, paths in TARGETS.items():
        for path in paths:
            args += ["--app-file", f"{app}={MEDIA / path}"]
    for app, paths in PEERS.items():
        for path in paths:
            args += ["--app-peer-pe", f"{app}={MEDIA / path}"]
    args += ["--json-out", str(ROOT / "benchmarks/pe-import-name-report-v1.json")]
    return main(args)


if __name__ == "__main__":
    raise SystemExit(run())
