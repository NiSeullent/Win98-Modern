"""Audit KernelEx API-table targets as source evidence, never as API support.

The names-only manifest is useful for discovery, but a DECL_API can point to an
UNIMPL_FUNC, a forwarding thunk, or a body that itself says it is a stub. This
small lexical audit keeps those cases visible in the whole-source inventory.
It deliberately does not compile code, evaluate #if branches, inspect the
installed guest, or infer semantic compatibility from a function body.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from measure_pe_coverage import pe_imports


SCHEMA = "w98mod.kernelex-source-body-audit.v1"
TABLE_RE = re.compile(r'DECL_TAB\s*\(\s*"([^"\r\n]+\.(?:DLL|DRV|OCX|CPL|ACM|AX|EXE))"', re.I)
API_RE = re.compile(r'DECL_API\s*\(\s*(?:"([^"\r\n]+)"|([0-9]+))\s*,\s*([A-Za-z_]\w*)\s*\)')
UNIMPL_RE = re.compile(r'\bUNIMPL_FUNC\s*\(\s*([A-Za-z_]\w*)\s*,')
FORWARD_RE = re.compile(r'\bFORWARD_TO_UNICOWS\s*\(\s*([A-Za-z_]\w*)\s*\)')
STUB_MARKER_RE = re.compile(r'\b(?:stub|unimplemented|not\s+implemented)\b', re.I)
DELEGATE_RE = re.compile(r'^\{\s*return\s+([A-Za-z_]\w*)\s*\([^;]*\)\s*;\s*\}$', re.S)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def lexical_mask(source: str, *, keep_strings: bool) -> str:
    """Blank comments, optionally literals, while preserving offsets/lines."""
    chars = list(source)
    i = 0
    while i < len(source):
        start = i
        if source.startswith("//", i):
            i = source.find("\n", i)
            if i < 0:
                i = len(source)
        elif source.startswith("/*", i):
            close = source.find("*/", i + 2)
            i = len(source) if close < 0 else close + 2
        elif source[i] in "\"'":
            quote = source[i]
            i += 1
            while i < len(source):
                if source[i] == "\\":
                    i += 2
                elif source[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            if keep_strings:
                continue
        else:
            i += 1
            continue
        for pos in range(start, min(i, len(source))):
            if chars[pos] != "\n":
                chars[pos] = " "
    return "".join(chars)


def matching_delimiter(code: str, start: int, opening: str, closing: str) -> int | None:
    depth = 0
    for pos in range(start, len(code)):
        if code[pos] == opening:
            depth += 1
        elif code[pos] == closing:
            depth -= 1
            if depth == 0:
                return pos
    return None


@dataclass(frozen=True)
class SourceFile:
    path: Path
    relative: str
    raw: str
    no_comments: str
    code: str

    @classmethod
    def read(cls, root: Path, path: Path) -> "SourceFile":
        raw = path.read_text(encoding="utf-8-sig", errors="replace")
        return cls(path, path.relative_to(root).as_posix(), raw,
                   lexical_mask(raw, keep_strings=True),
                   lexical_mask(raw, keep_strings=False))

    def location(self, pos: int) -> dict[str, str | int]:
        return {"file": self.relative, "line": self.raw.count("\n", 0, pos) + 1}

    def functions(self, symbol: str) -> list[dict]:
        matches: list[dict] = []
        pattern = re.compile(r"\b" + re.escape(symbol) + r"\s*\(")
        for match in pattern.finditer(self.code):
            opening = self.code.find("(", match.start())
            closing = matching_delimiter(self.code, opening, "(", ")")
            if closing is None:
                continue
            body_start = closing + 1
            while body_start < len(self.code) and self.code[body_start].isspace():
                body_start += 1
            if body_start >= len(self.code) or self.code[body_start] != "{":
                continue
            body_end = matching_delimiter(self.code, body_start, "{", "}")
            if body_end is None:
                continue
            body_code = self.code[body_start:body_end + 1]
            body_text = self.no_comments[body_start:body_end + 1]
            raw_body = self.raw[body_start:body_end + 1]
            marker = (STUB_MARKER_RE.search(body_text) or
                      STUB_MARKER_RE.search(raw_body))
            delegate = DELEGATE_RE.fullmatch(body_code)
            item = self.location(match.start())
            item["stub_marker"] = bool(marker)
            item["delegate_target"] = delegate.group(1) if delegate else None
            matches.append(item)
        return matches


def macro_locations(files: list[SourceFile], pattern: re.Pattern) -> dict[str, list[dict]]:
    found: dict[str, list[dict]] = {}
    for file in files:
        for match in pattern.finditer(file.no_comments):
            found.setdefault(match.group(1), []).append(file.location(match.start()))
    return found


def classify(target: str, symbol: str, files: list[SourceFile],
             unimpl: dict[str, list[dict]], forward: dict[str, list[dict]],
             native_exports: set[str]) -> tuple[str, list[dict]]:
    if target.endswith("_stub"):
        locations = unimpl.get(target[:-5], [])
        if locations:
            return "explicit_unimplemented_macro", locations
        bodies = [body for file in files for body in file.functions(target)]
        return ("explicit_stub_body" if bodies else "stub_named_unverified"), bodies
    if target.endswith("_fwd") and target[:-4] in forward:
        return "forward_to_unicows", forward[target[:-4]]
    bodies = [body for file in files for body in file.functions(target)]
    if len(bodies) > 1:
        return "ambiguous_multiple_bodies", bodies
    if bodies:
        body = bodies[0]
        if body["stub_marker"]:
            return "suspicious_stub_body", bodies
        if body["delegate_target"]:
            return "source_delegate_candidate", bodies
        return "source_body_candidate", bodies
    if target in native_exports:
        return "native_alias_candidate", []
    return "unresolved_target", []


def build(source_root: Path, native_manifest: Path | None = None,
          app_pe: Path | None = None, project_root: Path | None = None) -> dict:
    if app_pe is not None and native_manifest is None:
        raise ValueError("--app-pe requires --native-manifest for KEx-only comparison")
    source_root = source_root.resolve()
    modes = ("kexbases", "kexbasen")
    mode_files: dict[str, list[SourceFile]] = {}
    table_files: list[SourceFile] = []
    for mode in modes:
        mode_dir = source_root / "apilibs" / mode
        files = [SourceFile.read(source_root, path)
                 for path in sorted(mode_dir.rglob("*"))
                 if path.suffix.lower() in {".c", ".cpp"}]
        tables = [file for file in files if file.path.name.endswith("_apilist.c")]
        if not tables:
            raise ValueError(f"no API tables under {mode_dir}")
        mode_files[mode] = files
        table_files.extend(tables)
    native: dict[str, set[str]] = {}
    if native_manifest:
        data = json.loads(native_manifest.read_text(encoding="utf-8"))
        native = {dll.upper(): set(names) for dll, names in data["dlls"].items()}
    entries: list[dict] = []
    evidence_paths: set[str] = set()
    for mode in modes:
        files = mode_files[mode]
        unimpl = macro_locations(files, UNIMPL_RE)
        forward = macro_locations(files, FORWARD_RE)
        for table in (file for file in table_files if f"/{mode}/" in file.relative):
            dll_names = TABLE_RE.findall(table.no_comments)
            if len(dll_names) != 1:
                raise ValueError(f"expected one DECL_TAB in {table.relative}, got {dll_names}")
            dll = dll_names[0].upper()
            evidence_paths.add(table.relative)
            for match in API_RE.finditer(table.no_comments):
                symbol = match.group(1) or "#" + match.group(2)
                target = match.group(3)
                category, locations = classify(target, symbol, files, unimpl,
                                               forward, native.get(dll, set()))
                evidence_paths.update(item["file"] for item in locations)
                entries.append({"dll": dll, "symbol": symbol, "mode": mode,
                                "target": target,
                                "declaration": table.location(match.start()),
                                "source_category": category,
                                "target_evidence": locations})
    entries.sort(key=lambda item: (item["dll"], item["symbol"],
                                   item["mode"], item["declaration"]["file"],
                                   item["declaration"]["line"]))
    commit = subprocess.check_output(
        ["git", "-C", str(source_root), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(
        ["git", "-C", str(source_root), "status", "--porcelain"], text=True).strip())
    result = {
        "schema": SCHEMA,
        "evidence_kind": "lexical source triage; no compiled, installed, guest or behavioral proof",
        "source_git_commit": commit,
        "source_tree_dirty": dirty,
        "native_manifest_sha256": sha256(native_manifest) if native_manifest else None,
        "scope": "DECL_API rows in kexbases and kexbasen API tables; duplicate rows retained",
        "limitations": [
            "A source body is a candidate only; its ABI, errors, dependencies and behavior need review and guest tests.",
            "The lexer does not evaluate preprocessor conditions or CORE.INI route overrides.",
            "A native alias assumes only a matching name in the supplied ISO manifest, not installation or behavior.",
            "Stub markers are heuristic; code without them can still be a fake or incomplete implementation.",
        ],
        "summary": {"declaration_rows": len(entries),
                    "unique_dll_symbols": len({(x["dll"], x["symbol"]) for x in entries}),
                    "by_source_category": dict(sorted(Counter(x["source_category"] for x in entries).items()))},
        "source_file_sha256": {
            name: sha256(source_root / name) for name in sorted(evidence_paths)
        },
        "entries": entries,
    }
    if app_pe:
        imports = pe_imports(app_pe)
        keys = {(item["dll"], item["symbol"]) for item in entries}
        candidates = {(item["dll"], item["symbol"]) for item in imports
                      if (item["dll"], item["symbol"]) in keys and
                      item["symbol"] not in native.get(item["dll"], set())}
        selected = [item for item in entries if (item["dll"], item["symbol"]) in candidates]
        result["app_import_subset"] = {
            "app_pe": (app_pe.resolve().relative_to(project_root.resolve()).as_posix()
                       if project_root and app_pe.resolve().is_relative_to(project_root.resolve())
                       else app_pe.name),
            "app_pe_sha256": sha256(app_pe),
            "direct_import_occurrences": len(imports),
            "kernelex_only_unique_names": len(candidates),
            "entries": selected,
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--native-manifest", type=Path)
    parser.add_argument("--app-pe", type=Path)
    parser.add_argument("--project-root", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    result = build(args.source_root, args.native_manifest,
                   args.app_pe, args.project_root)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print(f"{args.out}: {result['summary']['declaration_rows']} rows, "
          f"{result['summary']['unique_dll_symbols']} unique module/names")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
