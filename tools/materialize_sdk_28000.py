"""Materialize pinned Windows SDK 10.0.28000.2705 NuGet inputs for inventory.

The Microsoft.Windows.SDK.CPP package contains headers, IDL, and Windows.winmd;
Microsoft.Windows.SDK.CPP.x86 contains the x86 UM import libraries. Neither
package's contents are committed. The output shape is the input layout expected
by build_sdk_inventory.py. The source package version is 10.0.28000.2705,
although Microsoft's internal directory version is 10.0.28000.0.
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath


VERSION = "10.0.28000.2705"
INTERNAL_VERSION = "10.0.28000.0"
PACKAGES = {
    "microsoft.windows.sdk.cpp": {
        "sha256": "a74ca8f9af98bd61925d9e2932ad98f746306123167b8f0bba99cd9ea9f03807",
        "size": 160512379,
    },
    "microsoft.windows.sdk.cpp.x86": {
        "sha256": "ffbe3d04ce5a81d182eb4771e9c5e57261f5769604918b37260358df1d1f44c4",
        "size": 50624132,
    },
}


def package_path(directory: Path, package: str) -> Path:
    return directory / f"{package}.{VERSION}.nupkg"


def verified_package(directory: Path, package: str, fetch: bool) -> Path:
    path = package_path(directory, package)
    if not path.exists() and fetch:
        directory.mkdir(parents=True, exist_ok=True)
        url = ("https://api.nuget.org/v3-flatcontainer/" + package + "/" + VERSION
               + "/" + path.name)
        temporary = path.with_suffix(".download")
        with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as target:
            shutil.copyfileobj(response, target, 1024 * 1024)
        temporary.replace(path)
    if not path.is_file():
        raise FileNotFoundError(f"missing {path}; use --fetch to retrieve it from NuGet")
    spec = PACKAGES[package]
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    if path.stat().st_size != spec["size"] or digest.hexdigest() != spec["sha256"]:
        raise ValueError(f"package bytes differ from pinned SHA-256: {path}")
    return path


def destination(member: str, package: str, root: Path) -> tuple[Path, str] | None:
    parts = PurePosixPath(member).parts
    if ".." in parts or PurePosixPath(member).is_absolute():
        raise ValueError(f"unsafe archive member: {member}")
    if package == "microsoft.windows.sdk.cpp":
        if len(parts) >= 5 and parts[:3] == ("c", "Include", INTERNAL_VERSION):
            if parts[3] in {"um", "shared"} and parts[-1].lower().endswith((".h", ".idl")):
                return root.joinpath("Include", VERSION, *parts[3:]), parts[-1].lower().rsplit(".", 1)[-1]
        if parts == ("c", "UnionMetadata", INTERNAL_VERSION, "Windows.winmd"):
            return root / "UnionMetadata" / VERSION / "Windows.winmd", "winmd"
    elif package == "microsoft.windows.sdk.cpp.x86":
        if len(parts) == 4 and parts[:3] == ("c", "um", "x86") and parts[-1].lower().endswith(".lib"):
            return root / "Lib" / VERSION / "um" / "x86" / parts[-1], "lib"
    return None


def materialize(packages: dict[str, Path], root: Path) -> dict[str, int]:
    counts = {"h": 0, "idl": 0, "lib": 0, "winmd": 0}
    seen: set[Path] = set()
    for package, archive_path in packages.items():
        with zipfile.ZipFile(archive_path) as archive:
            for info in archive.infolist():
                mapped = destination(info.filename, package, root)
                if not mapped or info.is_dir():
                    continue
                path, kind = mapped
                if path in seen:
                    raise ValueError(f"duplicate materialized path: {path}")
                seen.add(path)
                path.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as source, path.open("wb") as target:
                    shutil.copyfileobj(source, target, 1024 * 1024)
                counts[kind] += 1
    if not all(counts.values()):
        raise ValueError(f"incomplete SDK package inputs: {counts}")
    # A reused output root must not silently contribute files from another
    # package version to build_sdk_inventory.py's glob-based input selection.
    selected = [
        *root.joinpath("Include", VERSION, "um").rglob("*.h"),
        *root.joinpath("Include", VERSION, "shared").rglob("*.h"),
        *root.joinpath("Include", VERSION, "um").rglob("*.idl"),
        *root.joinpath("Lib", VERSION, "um", "x86").glob("*.lib"),
        root / "UnionMetadata" / VERSION / "Windows.winmd",
    ]
    stale = set(selected) - seen
    if stale:
        raise ValueError(f"unrecognized files in materialized SDK root: {sorted(stale)[:3]}")
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", type=Path, default=Path("build/sdk-28000"))
    parser.add_argument("--sdk-root", type=Path, default=Path("build/sdk-28000/materialized"))
    parser.add_argument("--fetch", action="store_true", help="download missing pinned packages from NuGet")
    args = parser.parse_args()
    packages = {name: verified_package(args.package_dir, name, args.fetch) for name in PACKAGES}
    counts = materialize(packages, args.sdk_root)
    print(f"SDK package version {VERSION}; internal directory version {INTERNAL_VERSION}")
    for name, path in packages.items():
        print(f"{name}: SHA-256 {PACKAGES[name]['sha256']} ({path.stat().st_size} bytes)")
    print(f"materialized {counts} under {args.sdk_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
