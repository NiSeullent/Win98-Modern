"""Prepare a backed-up CORE.INI candidate for the frozen COMCTL PNG provider.

This tool does not edit or install the guest CORE.INI. Supply a fresh copy of
the *current* guest file, its SHA-256, and the exact frozen M98CTLP.DLL.
The caller must install the provider, inspect the generated candidate, and
cold-boot separately. Never use an older GDI2-derived fixture as guest input.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

if __package__:
    from .patch_core_ordinals import _sections
else:
    from patch_core_ordinals import _sections


PREVIOUS_PROVIDERS = ("m98ctl1", "m98ctl2")
NEXT_PROVIDER = "m98ctl3"
FROZEN_DLL_SHA256 = (
    "aafd97b71ff63c9f9cedffa4705713243d5708f22ff6ffaf04c732261ca0e3f8"
)
ROUTE_SECTIONS = (
    b"DCFG1.ordinals.98",
    b"DCFG1.ordinals.Me",
    b"WINXP.ordinals",
)
ORDINALS = (345, 381)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def upgrade(data: bytes, from_provider: str,
            to_provider: str = NEXT_PROVIDER) -> bytes:
    """Replace exactly the registered provider and its six ordinal routes."""
    if from_provider not in PREVIOUS_PROVIDERS or to_provider != NEXT_PROVIDER:
        raise ValueError("unsupported COMCTL provider migration")
    old = from_provider.encode("ascii")
    new = to_provider.encode("ascii")
    if not data or not data.endswith((b"\n", b"\r")):
        raise ValueError("CORE.INI must be nonempty and newline terminated")
    if new in data.lower():
        raise ValueError("new provider is already referenced")
    sections = _sections(data)
    lookup = {name.lower(): lines for name, lines in sections}
    for required in (b"DCFG1", *ROUTE_SECTIONS):
        if required.lower() not in lookup:
            raise ValueError(f"missing section {required.decode()}")

    contents = lookup[b"dcfg1"]
    matching = [index for index, line in enumerate(contents)
                if line.rstrip(b"\r\n").startswith(b"contents=")]
    if len(matching) != 1:
        raise ValueError("DCFG1 must have one contents entry")
    index = matching[0]
    raw = contents[index]
    body = raw.rstrip(b"\r\n")
    ending = raw[len(body):]
    libraries = body[len(b"contents="):].split(b",")
    if libraries.count(old) != 1 or new in libraries or not all(libraries):
        raise ValueError("DCFG1 contents does not register old provider exactly once")
    contents[index] = b"contents=" + b",".join(
        new if library == old else library for library in libraries
    ) + ending

    for section in ROUTE_SECTIONS:
        lines = lookup[section.lower()]
        for ordinal in ORDINALS:
            prefix = b"COMCTL32." + str(ordinal).encode() + b"="
            matching = [index for index, line in enumerate(lines)
                        if line.rstrip(b"\r\n").startswith(prefix)]
            if len(matching) != 1:
                raise ValueError(f"{section.decode()} {ordinal} route count is not one")
            index = matching[0]
            raw = lines[index]
            body = raw.rstrip(b"\r\n")
            ending = raw[len(body):]
            if body != prefix + old + b".0":
                raise ValueError(f"{section.decode()} {ordinal} route changed")
            lines[index] = prefix + new + b".0" + ending

    if data.lower().count(old) != 7:
        raise ValueError("expected exactly seven old-provider references")
    result = b"".join(line for _, lines in sections for line in lines)
    if len(result) != len(data) or old in result.lower() or \
       result.lower().count(new) != 7:
        raise ValueError("provider reference postcondition failed")
    return result


def prepare(source: Path, candidate: Path, backup: Path, provider_dll: Path,
            expected_source_sha256: str, from_provider: str,
            to_provider: str = NEXT_PROVIDER,
            expected_provider_sha256: str = FROZEN_DLL_SHA256) -> dict[str, str | int]:
    """Create new backup/candidate files after hash and route validation."""
    paths = [path.resolve() for path in (source, candidate, backup, provider_dll)]
    if len(set(paths)) != len(paths):
        raise ValueError("source, candidate, backup and DLL paths must differ")
    if candidate.exists() or backup.exists():
        raise ValueError("candidate and backup paths must be new")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_source_sha256):
        raise ValueError("expected source SHA-256 must be lowercase hex")
    original = source.read_bytes()
    if digest(original) != expected_source_sha256:
        raise ValueError("CORE.INI source SHA-256 mismatch")
    dll = provider_dll.read_bytes()
    if digest(dll) != expected_provider_sha256:
        raise ValueError("frozen M98CTLP.DLL SHA-256 mismatch")
    updated = upgrade(original, from_provider, to_provider)
    with backup.open("xb") as stream:
        stream.write(original)
    if backup.read_bytes() != original:
        raise OSError("backup verification failed")
    with candidate.open("xb") as stream:
        stream.write(updated)
    if candidate.read_bytes() != updated or source.read_bytes() != original:
        raise OSError("candidate/source verification failed")
    return {
        "source_sha256": digest(original),
        "backup_sha256": digest(backup.read_bytes()),
        "candidate_sha256": digest(updated),
        "provider_sha256": digest(dll),
        "routes": 6,
        "from": from_provider,
        "to": to_provider,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--backup", required=True, type=Path)
    parser.add_argument("--provider-dll", required=True, type=Path)
    parser.add_argument("--expect-source-sha256", required=True)
    parser.add_argument("--from-provider", required=True,
                        choices=PREVIOUS_PROVIDERS)
    parser.add_argument("--to-provider", required=True,
                        choices=(NEXT_PROVIDER,))
    args = parser.parse_args()
    try:
        result = prepare(args.source, args.candidate, args.backup,
                         args.provider_dll, args.expect_source_sha256,
                         args.from_provider, args.to_provider)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
