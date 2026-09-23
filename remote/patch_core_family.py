"""Add reviewed single-DLL family routes without rewriting unrelated CORE bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SECTIONS = {b"DCFG1.names.98", b"DCFG1.names.Me", b"WINXP.names"}


def patch(data, library, names, dll="KERNEL32.DLL", table=0, register=False):
    if not re.fullmatch(r"[a-z][a-z0-9]{0,7}", library):
        raise ValueError("provider must be a lowercase DOS 8.3 stem")
    if not names or len(set(names)) != len(names) or any(not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", x) for x in names):
        raise ValueError("invalid or duplicate family names")
    if not re.fullmatch(r"[A-Z][A-Z0-9_]*\.DLL", dll) or type(table) is not int or not 0 <= table <= 255:
        raise ValueError("invalid target DLL or provider table index")
    provider = library.encode("ascii")
    target = dll[:-4].encode("ascii") + b"."
    provider_ref = provider + b"." + str(table).encode("ascii")
    encoded = [x.encode("ascii") for x in sorted(names)]
    output, section, seen, routes = [], b"", set(), set()
    provider_heading_seen = False
    contents_count = 0
    for line in data.splitlines(keepends=True):
        body = line.rstrip(b"\r\n")
        ending = line[len(body):]
        heading = re.fullmatch(rb"\[([^\]]+)\]", body)
        if heading:
            section = heading.group(1)
            if section == b"DCFG1":
                if provider_heading_seen:
                    raise ValueError("duplicate provider section")
                provider_heading_seen = True
            if section in SECTIONS:
                if section in seen or not ending:
                    raise ValueError("duplicate profile or unterminated profile heading")
                seen.add(section)
                output.append(line)
                for name in encoded:
                    output.append(target + name + b"=" + provider_ref + ending)
                continue
        if section == b"DCFG1" and body.startswith(b"contents="):
            contents_count += 1
            libraries = body[len(b"contents="):].split(b",")
            if contents_count != 1 or libraries.count(provider) > 1:
                raise ValueError("provider not uniquely registered")
            if provider not in libraries:
                if not register or not ending or not all(libraries):
                    raise ValueError("provider not registered; explicit registration required")
                line = body + b"," + provider + ending
        if section in SECTIONS:
            for name in encoded:
                prefix = target + name + b"="
                if body.strip().partition(b"=")[0].strip() == target + name and not body.startswith(prefix):
                    raise ValueError("malformed existing route")
                if body.startswith(prefix):
                    key = (section, name)
                    if key in routes or body != prefix + provider_ref:
                        raise ValueError("duplicate or conflicting existing route")
                    routes.add(key)
                    break
            else:
                output.append(line)
            continue
        output.append(line)
    if seen != SECTIONS or contents_count != 1 or not provider_heading_seen:
        raise ValueError("missing required profile or provider contents")
    return b"".join(output)


def select_family(manifest, family, target_dll=None):
    """A union is scoped to one target DLL/table, never every provider."""
    if manifest.get("schema") != "w98mod.runtime-routes.v2":
        raise ValueError("unsupported route manifest schema")
    families = manifest["families"]
    if family == "all":
        dll = target_dll or "KERNEL32.DLL"
        selected = [entry for entry in families.values() if entry["dll"] == dll]
    else:
        if family not in families:
            raise ValueError("unknown reviewed family")
        selected = [families[family]]
        dll = selected[0]["dll"]
        if target_dll and target_dll != dll:
            raise ValueError("family does not belong to requested target DLL")
    tables = {entry["table"] for entry in selected}
    if len(tables) != 1:
        raise ValueError("selection must use exactly one provider table")
    return dll, tables.pop(), sorted({name for entry in selected for name in entry["names"]})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--library", required=True)
    parser.add_argument("--family", required=True)
    parser.add_argument("--target-dll", help="scope 'all' to one target DLL; default KERNEL32.DLL")
    parser.add_argument("--register-provider", action="store_true",
                        help="append a new provider to DCFG1 contents")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "porting/runtime-routes.json").read_text(encoding="utf-8"))
    try:
        dll, table, names = select_family(manifest, args.family, args.target_dll)
    except ValueError as error:
        parser.error(str(error))
    if args.output.exists() or args.output.resolve() == args.source.resolve():
        parser.error("output must be a new path")
    original = args.source.read_bytes()
    updated = patch(original, args.library, names, dll, table, args.register_provider)
    with args.output.open("xb") as stream:
        stream.write(updated)
    print(json.dumps({"source_sha256":hashlib.sha256(original).hexdigest(),
        "output_sha256":hashlib.sha256(updated).hexdigest(), "family":args.family,
        "provider":args.library, "target_dll":dll, "table":table,
        "route_count":len(names)*len(SECTIONS)}))


if __name__ == "__main__":
    main()
