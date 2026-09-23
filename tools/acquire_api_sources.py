"""Acquire immutable source archives for the complete API porting inventory.

Archives stay in ignored build output. No archive is executed or extracted.
The receipt pins archive bytes as well as the upstream Git revision.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import urllib.request


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def acquire(source: dict, out: Path, old: dict) -> dict:
    name, revision = source["id"], source["revision"]
    if not re.fullmatch(r"[a-z0-9-]+", name) or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("source identity must be a safe name and full Git SHA")
    path = out / f"{name}-{revision}.tar.gz"
    url = source["archive_url"]
    previous = old.get(name, {})
    if (path.is_file() and previous.get("revision") == revision and
            previous.get("url") == url and previous.get("sha256") == sha256(path)):
        print(f"REUSE {name}: {path.stat().st_size} bytes", flush=True)
        return previous
    temporary = path.with_suffix(path.suffix + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": "Win98-Modern-source-inventory"})
    digest = hashlib.sha256()
    total = 0
    print(f"FETCH {name} at {revision}", flush=True)
    with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as output:
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            total += len(block)
            if total > 1024 * 1024 * 1024:
                raise ValueError(f"unexpected archive size for {name}")
            output.write(block)
            digest.update(block)
    if total < 1024:
        raise ValueError(f"implausibly short archive for {name}")
    os.replace(temporary, path)
    result = {"id": name, "revision": revision, "url": url,
              "path": path.relative_to(ROOT).as_posix(),
              "bytes": total, "sha256": digest.hexdigest()}
    print(f"READY {name}: {total} bytes sha256={result['sha256']}", flush=True)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "porting/sources.json")
    parser.add_argument("--out", type=Path, default=ROOT / "build/api-sources")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    out = args.out.resolve()
    out.relative_to(ROOT / "build")
    out.mkdir(parents=True, exist_ok=True)
    receipt_path = out / "receipt.json"
    old = {}
    if receipt_path.exists():
        old = {row["id"]: row for row in json.loads(receipt_path.read_text(encoding="utf-8"))["archives"]}
    archives = [row for row in manifest["sources"] if "archive_url" in row]
    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(lambda source: acquire(source, out, old), archives))
    receipt = {"schema": "w98mod.api-source-receipt.v1",
               "manifest_sha256": sha256(args.manifest),
               "archives": sorted(results, key=lambda row: row["id"])}
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"Source receipt: {receipt_path}")


if __name__ == "__main__":
    main()
