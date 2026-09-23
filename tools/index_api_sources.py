"""Index pinned upstream export declarations without treating names as code.

Reads immutable source tarballs in-place; never extracts or executes them.
Rows describe *declarations*, including stubs and forwards. They do not prove
that an implementation works on Win98. KernelEx and VxKex rows are read from
their pinned Git blobs, independent of checkout edits; the project API table
is explicitly labeled as working-tree data. VxKex is metadata-only by policy.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "w98mod.upstream-exports.v1"
SPEC_HEAD = re.compile(r"^\s*(?P<ordinal>@|\d+|0x[0-9a-fA-F]+)\s+"
                       r"(?P<type>[A-Za-z][A-Za-z0-9_]*)\s+(?P<body>.+?)\s*$")
SPEC_SYMBOL = re.compile(r"^(?P<name>[^\s(]+)(?:\s*\([^)]*\))?(?:\s+(?P<tail>.*))?$")
MACRO = re.compile(r'\b(?P<macro>DECL_API|M98_API)\s*\(\s*(?:"(?P<name>[^\"]+)"|(?P<number>\d+))\s*,\s*'
                   r'(?P<target>[A-Za-z_][A-Za-z0-9_]*)\s*\)')
DEF_ENTRY = re.compile(r"^(?P<name>[^\s=]+)(?:\s*=\s*(?P<target>[^\s]+))?"
                       r"(?:\s+@\s*(?P<ordinal>\d+))?(?:\s+(?P<flags>.*))?$")
LINKER_EXPORT = re.compile(r'^#\s*pragma\s+comment\s*\(\s*linker\s*,\s*'
                           r'"/EXPORT:(?P<name>[^=,\"]+)=(?P<target>[^,\"]+)'
                           r'(?P<options>(?:,[^\"]+)?)"\s*\)\s*$')
SPEC_TYPES = {"stdcall", "cdecl", "fastcall", "thiscall", "varargs",
              "pascal", "register", "stub", "extern", "variable", "equate"}
DATA_TYPES = {"extern", "variable", "equate"}
EXCLUDED_SEGMENTS = {"tests", "test", "rostests", "tools", "sdk", "samples",
                     "examples", "docs", "doc", "build", "boot", "freeldr"}
DEF_DIRECTIVES = {"LIBRARY", "NAME", "DESCRIPTION", "SECTIONS", "STACKSIZE",
                  "HEAPSIZE", "VERSION", "IMPORTS", "EXPORTS"}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_head(path: Path) -> str:
    result = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"],
                            check=True, capture_output=True, text=True)
    return result.stdout.strip()


def git_tree_files(path: Path, revision: str) -> list[str]:
    """List tracked paths in a commit, excluding untracked checkout files."""
    result = subprocess.run(["git", "-C", str(path), "ls-tree", "-r",
                             "--name-only", "-z", revision],
                            check=True, capture_output=True)
    return sorted(name.decode("utf-8") for name in result.stdout.split(b"\0")
                  if name)


def git_blob_bytes(path: Path, revision: str, relative: str) -> bytes:
    """Read the exact committed blob even when the checkout copy is dirty."""
    result = subprocess.run(["git", "-C", str(path), "cat-file", "blob",
                             f"{revision}:{relative}"],
                            check=True, capture_output=True)
    return result.stdout


def module_name(path: str, library: str | None = None) -> str:
    base = library or PurePosixPath(path).name.rsplit(".", 1)[0]
    base = base.strip('"')
    if not base:
        raise ValueError(f"empty module name for {path}")
    if not re.search(r"\.(dll|exe|exe16|drv|ocx|cpl|ax|sys|tsp|acm)$", base, re.I):
        base += ".dll"
    return base.upper()


def is_forward(target: str | None) -> bool:
    # Module-qualified spec targets are forwards; a plain C symbol is only
    # an internal alias. Keep the target text for later source inspection.
    return bool(target and re.fullmatch(r"[A-Za-z0-9_.-]+\.[^\s.]+", target))


def base_row(source: str, revision: str, dll: str, name: str, kind: str,
             target: str | None, ordinal: int | None, path: str, line: int,
             file_sha: str, *, flags: list[str] | None = None,
             declaration_type: str | None = None, raw: str = "") -> dict:
    flags = flags or []
    return {
        "source": source, "revision": revision, "key": f"{dll}!{name}",
        "dll": dll, "name": name, "kind": kind, "target": target,
        "ordinal": ordinal, "source_path": path, "line": line,
        "source_file_sha256": file_sha, "declaration_type": declaration_type,
        "flags": flags,
        "architecture_flags": [x for x in flags if x.startswith(("-arch=", "-i386", "-x86_64"))],
        "condition_flags": [x for x in flags if x.startswith(("-version=", "-arch=", "-i386", "-x86_64"))],
        "raw_declaration": raw,
    }


def strip_comment(line: str) -> str:
    # An ordinal forward has a meaningful embedded hash: DLL.#123.  Semicolon
    # comments in real .spec files may begin immediately after a declaration.
    quoted = False
    for index, char in enumerate(line):
        if char == '"':
            quoted = not quoted
        elif not quoted and (char == ";" or
                             (char == "#" and (index == 0 or line[index - 1].isspace()))):
            return line[:index].strip()
    return line.strip()


def parse_spec_line(raw: str, source: str, revision: str, path: str,
                    line: int, file_sha: str) -> tuple[dict | None, str | None]:
    content = strip_comment(raw)
    if not content:
        return None, None
    if content.startswith("apiset "):
        return None, "apiset_mapping_not_export"
    match = SPEC_HEAD.match(content)
    if not match:
        return None, "unrecognized_spec_syntax"
    call = match.group("type").lower()
    if call not in SPEC_TYPES:
        return None, "unrecognized_spec_type"
    body = match.group("body")
    flags: list[str] = []
    while body.startswith("-"):
        flag, separator, rest = body.partition(" ")
        if not separator:
            return None, "missing_symbol_after_spec_flags"
        flags.append(flag)
        body = rest.lstrip()
    symbol = SPEC_SYMBOL.match(body)
    if not symbol:
        return None, "unrecognized_spec_symbol"
    name = symbol.group("name")
    if not name or name.startswith("-"):
        return None, "invalid_spec_symbol"
    tail = symbol.group("tail") or ""
    target = tail.split()[0] if tail else None
    if tail and len(tail.split()) > 1:
        return None, "extra_spec_tokens"
    number = match.group("ordinal")
    ordinal = None if number == "@" else int(number, 0)
    if call == "stub" or "-stub" in flags:
        kind = "stub"
    elif call in DATA_TYPES:
        kind = "data"
    elif is_forward(target):
        kind = "forward"
    else:
        kind = "export_declaration"
    return base_row(source, revision, module_name(path), name, kind, target,
                    ordinal, path, line, file_sha, flags=flags,
                    declaration_type=call, raw=raw.strip()), None


def parse_def_text(data: str, source: str, revision: str, path: str,
                   file_sha: str) -> tuple[list[dict], list[dict]]:
    rows: list[dict] = []
    unresolved: list[dict] = []
    library: str | None = None
    in_exports = False
    for number, raw in enumerate(data.splitlines(), 1):
        content = strip_comment(raw)
        if not content:
            continue
        parts = content.split(None, 1)
        head, remainder = parts[0], (parts[1] if len(parts) > 1 else "")
        directive = head.upper()
        if directive == "LIBRARY" or directive == "NAME":
            if remainder.strip():
                library = remainder.strip().split()[0]
            in_exports = False
            continue
        if directive == "EXPORTS":
            in_exports = True
            content = remainder.strip()
            if not content:
                continue
        elif directive in DEF_DIRECTIVES:
            in_exports = False
            continue
        if not in_exports:
            # Linker metadata outside an EXPORTS section is not a symbol.
            continue
        match = DEF_ENTRY.match(content)
        if not match:
            unresolved.append({"source": source, "source_path": path,
                               "line": number, "raw": raw,
                               "reason": "unrecognized_def_export"})
            continue
        name = match.group("name")
        target = match.group("target")
        flags = (match.group("flags") or "").split()
        ordinal = int(match.group("ordinal")) if match.group("ordinal") else None
        if any(flag.upper() == "DATA" for flag in flags):
            kind = "data"
        elif is_forward(target):
            kind = "forward"
        else:
            kind = "export_declaration"
        rows.append(base_row(source, revision, module_name(path, library),
                             name, kind, target, ordinal, path, number,
                             file_sha, flags=flags, declaration_type="def",
                             raw=raw.strip()))
    return rows, unresolved


def parse_macro_line(raw: str, source: str, revision: str, path: str,
                     line: int, file_sha: str) -> tuple[dict | None, str | None]:
    content = raw.strip()
    if content.startswith(("#", "//", "/*", "*")) or not re.search(r"\b(DECL_API|M98_API)\s*\(", content):
        return None, None
    match = MACRO.search(content)
    if not match:
        return None, "unrecognized_api_macro"
    if (source == "kernelex" and match.group("macro") != "DECL_API") or (
            source == "project" and match.group("macro") != "M98_API"):
        return None, "unexpected_api_macro"
    numeric = match.group("number")
    name, target = match.group("name") or f"#{numeric}", match.group("target")
    if "*/" in name or not name.strip() or any(c.isspace() for c in name):
        return None, "noncanonical_api_name"
    if source == "project":
        dll = "KERNEL32.DLL"  # Current M98_API table in src/m98wrap.c.
    else:
        dll = module_name(PurePosixPath(path).parent.name)
    kind = "forward" if target.lower().endswith("_fwd") else (
        "stub" if target.lower().endswith("_stub") else "export_declaration")
    return base_row(source, revision, dll, name, kind, target,
                    int(numeric) if numeric else None,
                    path, line, file_sha, flags=[],
                    declaration_type=match.group("macro"), raw=content), None


def record_unresolved(source: str, path: str, line: int, raw: str,
                      reason: str) -> dict:
    return {"source": source, "source_path": path, "line": line,
            "raw": raw, "reason": reason}


def runtime_file(path: str) -> tuple[bool, str | None]:
    parts = PurePosixPath(path).parts
    excluded = EXCLUDED_SEGMENTS.intersection(part.lower() for part in parts[:-1])
    if excluded:
        return False, "nonruntime_path:" + sorted(excluded)[0]
    return True, None


def c_macro_lines(text: str):
    """Yield uncommented C lines with their preprocessor branch context."""
    conditions: list[str] = []
    block_comment = False
    for line_number, raw in enumerate(text.splitlines(), 1):
        code = []
        i = 0
        while i < len(raw):
            if block_comment:
                end = raw.find("*/", i)
                if end < 0:
                    i = len(raw)
                else:
                    block_comment = False
                    i = end + 2
            elif raw.startswith("/*", i):
                block_comment = True
                i += 2
            elif raw.startswith("//", i):
                break
            else:
                code.append(raw[i])
                i += 1
        content = "".join(code).strip()
        if re.match(r"^#\s*(?:if|ifdef|ifndef)\b", content):
            conditions.append(content)
            continue
        if re.match(r"^#\s*(?:elif|else)\b", content):
            if conditions:
                origin = conditions[-1].split(" | branch:", 1)[0]
                conditions[-1] = origin + " | branch:" + content
            continue
        if re.match(r"^#\s*endif\b", content):
            if conditions:
                conditions.pop()
            continue
        yield line_number, content, list(conditions)


def index_text(data: bytes, source: str, revision: str,
               path: str) -> tuple[list[dict], list[dict]]:
    text = data.decode("utf-8-sig", errors="replace")
    file_sha = sha256_bytes(data)
    if path.lower().endswith(".def"):
        return parse_def_text(text, source, revision, path, file_sha)
    rows: list[dict] = []
    unresolved: list[dict] = []
    for line, raw in enumerate(text.splitlines(), 1):
        row, reason = parse_spec_line(raw, source, revision, path, line, file_sha)
        if row:
            rows.append(row)
        elif reason:
            unresolved.append(record_unresolved(source, path, line, raw, reason))
    return rows, unresolved


def verified_archive(source: dict, receipt: dict, root: Path) -> Path:
    expected = f"{source['id']}-{source['revision']}.tar.gz"
    path = (root / "build/api-sources" / expected).resolve()
    if path.name != expected or not path.is_file():
        raise ValueError(f"missing pinned archive {expected}")
    if (receipt.get("id") != source["id"] or
            receipt.get("revision") != source["revision"] or
            receipt.get("url") != source["archive_url"]):
        raise ValueError(f"archive receipt identity mismatch: {source['id']}")
    if receipt.get("path") != path.relative_to(root).as_posix():
        raise ValueError(f"archive receipt path mismatch: {source['id']}")
    if receipt.get("bytes") != path.stat().st_size or receipt.get("sha256") != sha256_file(path):
        raise ValueError(f"archive SHA/size mismatch: {source['id']}")
    return path


def archive_rows(source: dict, archive: Path) -> tuple[list[dict], list[dict], list[dict], str]:
    rows: list[dict] = []
    unresolved: list[dict] = []
    excluded: list[dict] = []
    top_prefix: str | None = None
    with tarfile.open(archive, "r:gz") as package:
        for member in package:
            parts = PurePosixPath(member.name).parts
            if not parts or ".." in parts or member.name.startswith("/"):
                raise ValueError(f"unsafe archive member: {member.name}")
            if top_prefix is None:
                top_prefix = parts[0]
                if not top_prefix.endswith("-" + source["revision"]):
                    raise ValueError(f"archive revision prefix mismatch: {archive.name}")
            elif parts[0] != top_prefix:
                raise ValueError(f"multiple archive roots: {archive.name}")
            if not member.name.lower().endswith((".spec", ".def")):
                continue
            path = PurePosixPath(*parts[1:]).as_posix()
            accepted, reason = runtime_file(path)
            if not accepted:
                excluded.append({"source": source["id"], "source_path": path,
                                 "reason": reason})
                continue
            if not member.isfile():
                excluded.append({"source": source["id"], "source_path": path,
                                 "reason": "nonregular_archive_member"})
                continue
            stream = package.extractfile(member)
            if stream is None:
                raise ValueError(f"cannot read {member.name}")
            data = stream.read()
            if path.lower().endswith(".spec") and data.lstrip().startswith(b"Summary:"):
                excluded.append({"source": source["id"], "source_path": path,
                                 "reason": "non_windows_package_spec"})
                continue
            found, missed = index_text(data, source["id"], source["revision"], path)
            rows.extend(found)
            unresolved.extend(missed)
    return rows, unresolved, excluded, top_prefix or ""


def local_macro_rows(source: dict, root: Path) -> tuple[list[dict], list[dict]]:
    local = (root / source["local_root"]).resolve()
    if git_head(local) != source["revision"]:
        raise ValueError(f"local Git revision mismatch: {source['id']}")
    if source["id"] == "vxkex":
        return vxkex_metadata_rows(source, root, local)
    rows: list[dict] = []
    unresolved: list[dict] = []
    for git_path in git_tree_files(local, source["revision"]):
        pure = PurePosixPath(git_path)
        if not (git_path.startswith("apilibs/") and pure.name.endswith("_apilist.c")):
            continue
        raw_data = git_blob_bytes(local, source["revision"], git_path)
        sha = sha256_bytes(raw_data)
        relative = (local / Path(*pure.parts)).relative_to(root).as_posix()
        for line, raw, conditions in c_macro_lines(raw_data.decode("utf-8-sig", "replace")):
            row, reason = parse_macro_line(raw, source["id"], source["revision"], relative, line, sha)
            if row:
                row["condition_flags"].extend(conditions)
                rows.append(row)
            elif reason:
                unresolved.append(record_unresolved(source["id"], relative, line, raw, reason))
    return rows, unresolved


def vxkex_metadata_rows(source: dict, root: Path,
                        local: Path) -> tuple[list[dict], list[dict]]:
    """Record VxKex export metadata without carrying any implementation text."""
    rows: list[dict] = []
    unresolved: list[dict] = []
    tracked = git_tree_files(local, source["revision"])
    for git_path in (x for x in tracked if PurePosixPath(x).suffix.lower() == ".def"):
        data = git_blob_bytes(local, source["revision"], git_path)
        relative = (local / Path(*PurePosixPath(git_path).parts)).relative_to(root).as_posix()
        found, missed = parse_def_text(data.decode("utf-8-sig", "replace"),
                                       "vxkex", source["revision"], relative,
                                       sha256_bytes(data))
        for row in found:
            row.pop("raw_declaration", None)
            row["reuse_policy"] = "reference_metadata_only"
        rows.extend(found)
        unresolved.extend(missed)
    for git_path in (x for x in tracked if PurePosixPath(x).name.lower() == "forwards.c"):
        data = git_blob_bytes(local, source["revision"], git_path)
        relative = (local / Path(*PurePosixPath(git_path).parts)).relative_to(root).as_posix()
        file_sha = sha256_bytes(data)
        dll = module_name(PurePosixPath(git_path).parent.name)
        for line, content, conditions in c_macro_lines(data.decode("utf-8-sig", "replace")):
            if "/EXPORT:" not in content:
                continue
            match = LINKER_EXPORT.match(content)
            if not match:
                unresolved.append(record_unresolved("vxkex", relative, line,
                                                    "<linker export declaration>",
                                                    "unrecognized_linker_export"))
                continue
            options = [x.strip() for x in match.group("options").split(",") if x.strip()]
            ordinal = None
            flags: list[str] = []
            for option in options:
                if option.startswith("@") and option[1:].isdigit():
                    ordinal = int(option[1:])
                else:
                    flags.append(option)
            target = match.group("target")
            row = base_row("vxkex", source["revision"], dll,
                           match.group("name"), "forward" if is_forward(target)
                           else "export_declaration", target, ordinal,
                           relative, line, file_sha, flags=flags,
                           declaration_type="linker_pragma")
            row.pop("raw_declaration", None)
            row["reuse_policy"] = "reference_metadata_only"
            row["condition_flags"].extend(conditions)
            rows.append(row)
    return rows, unresolved


def project_macro_rows(root: Path) -> tuple[list[dict], list[dict], str]:
    revision = "working-tree@" + git_head(root)
    rows: list[dict] = []
    unresolved: list[dict] = []
    for path in sorted((root / "src").glob("*.c")):
        raw_data = path.read_bytes()
        if b"M98_API(" not in raw_data:
            continue
        sha = sha256_bytes(raw_data)
        relative = path.relative_to(root).as_posix()
        for line, raw, conditions in c_macro_lines(raw_data.decode("utf-8-sig", "replace")):
            row, reason = parse_macro_line(raw, "project", revision, relative, line, sha)
            if row:
                row["condition_flags"].extend(conditions)
                rows.append(row)
            elif reason:
                unresolved.append(record_unresolved("project", relative, line, raw, reason))
    return rows, unresolved, revision


def build_index(manifest_path: Path, receipt_path: Path,
                output_dir: Path, root: Path = ROOT) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if (manifest.get("schema") != "w98mod.porting-sources.v1" or
            receipt.get("schema") != "w98mod.api-source-receipt.v1"):
        raise ValueError("unexpected source manifest or receipt schema")
    if receipt.get("manifest_sha256") != sha256_file(manifest_path):
        raise ValueError("source manifest SHA mismatch against receipt")
    receipts = {x["id"]: x for x in receipt["archives"]}
    rows: list[dict] = []
    unresolved: list[dict] = []
    excluded: list[dict] = []
    verified: list[dict] = []
    for source in manifest["sources"]:
        name = source["id"]
        if "archive_url" in source:
            archive = verified_archive(source, receipts.get(name, {}), root)
            found, missed, skipped, prefix = archive_rows(source, archive)
            verified.append({"source": name, "revision": source["revision"],
                             "archive_sha256": receipts[name]["sha256"],
                             "archive_root": prefix, "policy": "declarations_only"})
            rows.extend(found); unresolved.extend(missed); excluded.extend(skipped)
        elif "local_root" in source:
            found, missed = local_macro_rows(source, root)
            verified.append({"source": name, "revision": source["revision"],
                             "local_git_head_verified": True,
                             "local_bytes_source": "pinned_git_blobs_including_sparse_paths",
                             "policy": "metadata_only" if name == "vxkex" else "declarations_only"})
            rows.extend(found); unresolved.extend(missed)
        else:
            raise ValueError(f"source lacks archive/local_root: {name}")
    project, project_unresolved, project_revision = project_macro_rows(root)
    rows.extend(project); unresolved.extend(project_unresolved)
    verified.append({"source": "project", "revision": project_revision,
                     "policy": "working_tree_declarations_only"})
    rows.sort(key=lambda x: (x["source"], x["source_path"], x["line"], x["name"]))
    output_dir.mkdir(parents=True, exist_ok=True)
    jsonl = output_dir / "upstream-exports.jsonl"
    with jsonl.open("w", encoding="utf-8", newline="\n") as stream:
        for row in rows:
            stream.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
    by_source = Counter(row["source"] for row in rows)
    by_kind = Counter(row["kind"] for row in rows)
    summary = {
        "schema": SCHEMA, "manifest_sha256": sha256_file(manifest_path),
        "receipt_sha256": sha256_file(receipt_path),
        "jsonl_sha256": sha256_file(jsonl), "row_count": len(rows),
        "unique_dll_name_keys": len({row["key"] for row in rows}),
        "rows_by_source": dict(sorted(by_source.items())),
        "rows_by_kind": dict(sorted(by_kind.items())),
        "verified_sources": verified,
        "excluded_file_count": len(excluded), "excluded_files": excluded,
        "unparsed_line_count": len(unresolved), "unparsed_lines": unresolved,
        "interpretation": "A declaration, including an export_declaration, is not proof of implemented behavior; stub/forward/data are explicitly separate.",
    }
    (output_dir / "upstream-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "porting/sources.json")
    parser.add_argument("--receipt", type=Path, default=ROOT / "build/api-sources/receipt.json")
    parser.add_argument("--out", type=Path, default=ROOT / "build/api-catalog")
    args = parser.parse_args()
    summary = build_index(args.manifest.resolve(), args.receipt.resolve(),
                          args.out.resolve())
    print(f"Indexed {summary['row_count']} declarations / "
          f"{summary['unique_dll_name_keys']} DLL+name keys; "
          f"{summary['unparsed_line_count']} unparsed lines, "
          f"{summary['excluded_file_count']} excluded files")
    print(args.out.resolve() / "upstream-exports.jsonl")


if __name__ == "__main__":
    main()
