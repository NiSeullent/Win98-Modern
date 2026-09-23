"""Add reviewed KERNEL32 family routes without rewriting unrelated CORE bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}


def patch(data, library, names):
    if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
        raise ValueError("provider must be a lowercase DOS 8.3 stem")
    if not names or len(set(names)) != len(names) or any(not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", x) for x in names):
        raise ValueError("invalid or duplicate family names")
    provider = library.encode("ascii")
    encoded = [x.encode("ascii") for x in sorted(names)]
    output, section, seen, routes = [], b"", set(), set()
    contents_count = 0
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        heading = re.fullmatch(rb"\[([^\]]+)\]", body)
        if heading:
            section = heading.group(1)
            if section in SECTIONS:
                if section in seen or not ending:
                    raise ValueError("duplicate profile or unterminated profile heading")
                seen.add(section)
                output.append(line)
                for name in encoded:
                    output.append(b"KERNEL32." + name + b"=" + provider + b".0" + ending)
                continue
        if section == b"DCFG1" and body.startswith(b"contents="):
            contents_count += 1
            if contents_count != 1 or body[len(b"contents="):].split(b",").count(provider) != 1:
                raise ValueError("provider not uniquely registered")
        if section in SECTIONS:
            for name in encoded:
                prefix = b"KERNEL32." + name + b"="
                if body.startswith(prefix):
                    key = (section, name)
                    if key in routes or body != prefix + provider + b".0":
                        raise ValueError("duplicate or conflicting existing route")
                    routes.add(key)
                    break
            else:
                output.append(line)
            continue
        output.append(line)
    if seen != SECTIONS or contents_count != 1:
        raise ValueError("missing required profile or provider contents")
    return b"".join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--library", required=True)
    parser.add_argument("--family", required=True)
    args = parser.parse_args()
    families = json.loads((ROOT / "porting/runtime-routes.json").read_text(encoding="utf-8"))["families"]
    if args.family != "all" and args.family not in families:
        parser.error("unknown reviewed family")
    names = (sorted({name for group in families.values() for name in group})
             if args.family == "all" else families[args.family])
    if args.output.exists() or args.output.resolve() == args.source.resolve():
        parser.error("output must be a new path")
    original = args.source.read_bytes()
    updated = patch(original, args.library, names)
    with args.output.open("xb") as stream:
        stream.write(updated)
    print(json.dumps({"source_sha256":hashlib.sha256(original).hexdigest(),
        "output_sha256":hashlib.sha256(updated).hexdigest(), "family":args.family,
        "provider":args.library, "route_count":len(names)*len(SECTIONS)}))


if __name__ == "__main__":
    main()
