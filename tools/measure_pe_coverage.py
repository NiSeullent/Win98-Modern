"""Measure declared PE import-name coverage for curated 32-bit app binaries.

This is an import-inventory aid, not an application runtime or Windows API
surface compatibility score. See docs/PE_IMPORT_COVERAGE.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Iterable

from scan_imports import PEError, PEView


SCHEMA = "w98mod.export-manifest.v1"
PE32_I386 = 0x14C
MAX_TABLE_ITEMS = 65536
SYMBOL_RE = re.compile(r"#(?:0|[1-9][0-9]*)$|[^\s]+$")


class CoverageError(ValueError):
    """Invalid input to the coverage measurement."""


def normalize_dll(value: str) -> str:
    dll = value.replace("\\", "/").rsplit("/", 1)[-1].upper()
    if not dll:
        raise CoverageError("empty DLL name")
    if "." not in dll:
        dll += ".DLL"
    return dll


def add_symbols(destination: dict[str, set[str]], dll: str,
                symbols: Iterable[str]) -> None:
    bucket = destination.setdefault(normalize_dll(dll), set())
    for symbol in symbols:
        if not isinstance(symbol, str) or not SYMBOL_RE.fullmatch(symbol):
            raise CoverageError(f"invalid export symbol {symbol!r} in {dll}")
        bucket.add(symbol)


def merge_symbols(destination: dict[str, set[str]],
                  source: dict[str, set[str]]) -> None:
    for dll, symbols in source.items():
        add_symbols(destination, dll, symbols)


def ensure_pe32_i386(view: PEView, path: Path) -> None:
    pe_offset = view.u32(0x3C)
    machine = view.u16(pe_offset + 4)
    if machine != PE32_I386 or view.thunk_width != 4:
        raise CoverageError(
            f"{path}: expected 32-bit x86 PE (machine 0x14c, PE32); "
            f"got machine {machine:#x}, thunk width {view.thunk_width}"
        )


def directory(view: PEView, index: int) -> tuple[int, int]:
    pe_offset = view.u32(0x3C)
    optional_size = view.u16(pe_offset + 20)
    optional = pe_offset + 24
    if view.u16(optional) != 0x10B:
        raise PEError("expected PE32 optional header")
    if optional_size < 96:
        raise PEError("truncated PE32 optional header")
    count = view.u32(optional + 92)
    if index >= count:
        return 0, 0
    entry = optional + 96 + index * 8
    if entry + 8 > optional + optional_size:
        raise PEError("data directory exceeds optional header")
    return view.u32(entry), view.u32(entry + 4)


def thunk_symbols(view: PEView, thunk_rva: int) -> list[str]:
    if not thunk_rva:
        raise PEError("missing import name thunk table")
    offset = view.rva(thunk_rva)
    result: list[str] = []
    for index in range(MAX_TABLE_ITEMS):
        value = view.u32(offset + index * 4)
        if value == 0:
            return result
        if value & 0x80000000:
            result.append(f"#{value & 0xFFFF}")
        else:
            result.append(view.string(value + 2))
    raise PEError("unterminated import name thunk table")


def delay_imports(view: PEView) -> list[tuple[str, str]]:
    delay_rva, delay_size = directory(view, 13)
    if not delay_rva:
        return []
    offset = view.rva(delay_rva)
    image_base = view.u32(view.u32(0x3C) + 24 + 28)
    result: list[tuple[str, str]] = []
    limit = min(MAX_TABLE_ITEMS, delay_size // 32) if delay_size else MAX_TABLE_ITEMS
    if limit == 0:
        raise PEError("delay import directory is shorter than one descriptor")
    for index in range(limit):
        values = view.unpack("<IIIIIIII", offset + index * 32)
        if not any(values):
            return result
        attrs, name, _handle, iat, names, _bound, _unload, _stamp = values
        if not attrs & 1:
            if name < image_base or iat < image_base or (names and names < image_base):
                raise PEError("delay import VA precedes image base")
            name -= image_base
            iat -= image_base
            if names:
                names -= image_base
        dll = normalize_dll(view.string(name))
        for symbol in thunk_symbols(view, names or iat):
            result.append((dll, symbol))
    raise PEError("unterminated delay import directory")


def pe_imports(path: Path) -> list[dict[str, str]]:
    view = PEView(path)
    ensure_pe32_i386(view, path)
    result = [
        {"dll": normalize_dll(dll), "symbol": symbol, "kind": "load"}
        for dll, symbol in view.imports()
    ]
    result.extend(
        {"dll": dll, "symbol": symbol, "kind": "delay"}
        for dll, symbol in delay_imports(view)
    )
    return result


def pe_exports(path: Path, override_dll: str | None = None) -> dict[str, set[str]]:
    view = PEView(path)
    ensure_pe32_i386(view, path)
    export_rva, _export_size = directory(view, 0)
    if not export_rva:
        raise CoverageError(f"{path}: no PE export directory")
    offset = view.rva(export_rva)
    header = view.unpack("<IIHHIIIIIII", offset)
    name_rva, ordinal_base, function_count, name_count = header[4:8]
    functions_rva, names_rva, name_ordinals_rva = header[8:11]
    if function_count > MAX_TABLE_ITEMS or name_count > MAX_TABLE_ITEMS:
        raise PEError("export table has too many entries")
    dll = override_dll or (view.string(name_rva) if name_rva else path.name)
    symbols: set[str] = set()
    functions_offset = view.rva(functions_rva) if function_count else 0
    for index in range(function_count):
        if view.u32(functions_offset + index * 4):
            symbols.add(f"#{ordinal_base + index}")
    names_offset = view.rva(names_rva) if name_count else 0
    ordinals_offset = view.rva(name_ordinals_rva) if name_count else 0
    for index in range(name_count):
        function_index = view.u16(ordinals_offset + index * 2)
        if function_index >= function_count:
            raise PEError("export name points outside function table")
        if view.u32(functions_offset + function_index * 4) == 0:
            raise PEError("export name points to empty function slot")
        symbols.add(view.string(view.u32(names_offset + index * 4)))
    result: dict[str, set[str]] = {}
    add_symbols(result, dll, symbols)
    return result


def source_spec(value: str) -> tuple[str | None, Path]:
    if "=" in value:
        maybe_dll, maybe_path = value.split("=", 1)
        if re.fullmatch(r"[^/\\]+\.dll", maybe_dll, flags=re.I):
            path = Path(maybe_path)
            if not path.exists():
                raise CoverageError(f"source/artifact path does not exist: {path}")
            return normalize_dll(maybe_dll), path
    path = Path(value)
    if not path.exists():
        raise CoverageError(f"source/artifact path does not exist: {path}")
    return None, path


def strip_c_comments(source: str) -> str:
    pattern = re.compile(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*', re.S)
    return pattern.sub(
        lambda match: match.group(0) if match.group(0).startswith('"') else " ",
        source,
    )


def def_exports(path: Path, override_dll: str | None) -> dict[str, set[str]]:
    source = path.read_text(encoding="utf-8-sig", errors="replace")
    library = re.search(r'^\s*LIBRARY\s+"?([^\s"]+)', source, re.I | re.M)
    dll = override_dll or (library.group(1) if library else path.stem + ".DLL")
    in_exports = False
    symbols: set[str] = set()
    for line in source.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        if re.match(r"^EXPORTS(?:\s|$)", line, re.I):
            in_exports = True
            continue
        if not in_exports:
            continue
        if re.match(r"^(LIBRARY|SECTIONS|DESCRIPTION|STACKSIZE|HEAPSIZE)\b", line, re.I):
            in_exports = False
            continue
        first = line.split()[0]
        name = first.split("=", 1)[0]
        ordinal = re.search(r"(?:^|\s)@([0-9]+)(?:\s|$)", line)
        if ordinal:
            symbols.add(f"#{int(ordinal.group(1))}")
        if name and not re.search(r"\bNONAME\b", line, re.I):
            symbols.add(name)
    result: dict[str, set[str]] = {}
    add_symbols(result, dll, symbols)
    return result


def c_table_exports(path: Path, override_dll: str | None) -> dict[str, set[str]]:
    source = strip_c_comments(path.read_text(encoding="utf-8", errors="replace"))
    result: dict[str, set[str]] = {}
    # KernelEx providers bind typed named and ordinal arrays through an
    # m98_api_table. Only that binding identifies the target DLL; a macro or
    # array elsewhere in the file does not declare a route for this report.
    arrays = {
        match.group("name"): (match.group("type"), match.group("body"))
        for match in re.finditer(
            r"\bm98_(?P<type>named|ordinal)_api\s+"
            r"(?P<name>[A-Za-z_]\w*)\s*\[\s*\]\s*=\s*"
            r"\{(?P<body>.*?)\}\s*;", source, re.S,
        )
    }
    bindings: dict[tuple[str, str], set[str]] = {}
    for table in re.finditer(
        r"\bm98_api_table\s+[A-Za-z_]\w*\s*\[\s*\]\s*=\s*"
        r"\{(?P<body>.*?)\}\s*;", source, re.S,
    ):
        for entry in re.finditer(
            r'\{\s*"(?P<dll>[A-Za-z0-9_]+\.(?:DLL|DRV|OCX|CPL|ACM|AX|EXE))"'
            r"\s*,\s*(?P<named>[A-Za-z_]\w*|0|NULL)\s*,\s*[^,{}]*,"
            r"\s*(?P<ordinal>[A-Za-z_]\w*|0|NULL)\s*,\s*[^,{}]*\}",
            table.group("body"), re.I | re.S,
        ):
            dll = normalize_dll(override_dll or entry.group("dll"))
            for category in ("named", "ordinal"):
                name = entry.group(category)
                if name not in {"0", "NULL"} and name in arrays:
                    if arrays[name][0] != category:
                        raise CoverageError(f"{path}: {name} bound as wrong API array type")
                    bindings.setdefault((category, name), set()).add(dll)
    for (category, name), dlls in bindings.items():
        if len(dlls) != 1:
            raise CoverageError(f"{path}: {name} bound to multiple target DLLs")
        body = arrays[name][1]
        if category == "named":
            symbols = re.findall(
                r'\b(?:M98_API|M98_NAMED)\s*\(\s*"([^"\r\n]+)"\s*,', body)
            symbols.extend(re.findall(
                r'\{\s*"([^"\r\n]+)"\s*,\s*(?:\([^()]*\)\s*)?'
                r'[A-Za-z_]\w*\s*\}', body))
        else:
            symbols = [f"#{int(number)}" for number in re.findall(
                r"\bM98_ORD\s*\(\s*([0-9]+)\s*,", body)]
        add_symbols(result, next(iter(dlls)), symbols)
    # KernelEx also exposes WINSPOOL.DRV. A DECL_TAB names a PE module,
    # whose extension need not be .DLL (other common loadable PE modules are
    # .OCX/.CPL/.ACM/.AX, and an .EXE can export symbols as well).
    tables = re.findall(
        r'DECL_TAB\s*\(\s*"([^"\r\n]+\.(?:DLL|DRV|OCX|CPL|ACM|AX|EXE))"',
        source, re.I,
    )
    if tables:
        if len(set(map(normalize_dll, tables))) != 1 and not override_dll:
            raise CoverageError(f"{path}: multiple DECL_TAB DLLs; split the source")
        names = re.findall(r'DECL_API\s*\(\s*"([^"\r\n]+)"', source)
        names.extend(
            f"#{number}" for number in re.findall(
                r'DECL_API\s*\(\s*([0-9]+)\s*,', source
            )
        )
        add_symbols(result, override_dll or tables[0], names)
    return result


def source_exports(specs: Iterable[str]) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    visited: set[Path] = set()
    for spec in specs:
        override_dll, root = source_spec(spec)
        paths = sorted(root.rglob("*")) if root.is_dir() else [root]
        for path in paths:
            if not path.is_file() or path.suffix.lower() not in {".c", ".h", ".def"}:
                continue
            resolved = path.resolve()
            if resolved in visited:
                continue
            visited.add(resolved)
            parsed = (def_exports(path, override_dll) if path.suffix.lower() == ".def"
                      else c_table_exports(path, override_dll))
            merge_symbols(result, parsed)
    if not any(result.values()):
        raise CoverageError("no supported API-table or .def export declarations found")
    return result


def artifact_exports(specs: Iterable[str]) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    for spec in specs:
        override_dll, path = source_spec(spec)
        if path.is_dir():
            raise CoverageError(f"PE artifact must be a file: {path}")
        merge_symbols(result, pe_exports(path, override_dll))
    return result


def write_manifest(path: Path, exports: dict[str, set[str]],
                   provenance: str) -> None:
    if not provenance.strip():
        raise CoverageError("manifest provenance cannot be empty")
    manifest = {
        "schema": SCHEMA,
        "provenance": provenance,
        "dlls": {dll: sorted(symbols) for dll, symbols in sorted(exports.items())},
    }
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def load_manifest(path: Path) -> tuple[dict[str, set[str]], str]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise CoverageError(f"{path}: expected schema {SCHEMA}")
    provenance = manifest.get("provenance")
    if not isinstance(provenance, str) or not provenance.strip():
        raise CoverageError(f"{path}: missing baseline provenance")
    dlls = manifest.get("dlls")
    if not isinstance(dlls, dict):
        raise CoverageError(f"{path}: dlls must be a DLL-to-symbol-list mapping")
    exports: dict[str, set[str]] = {}
    for dll, symbols in dlls.items():
        if not isinstance(dll, str) or not isinstance(symbols, list):
            raise CoverageError(f"{path}: invalid DLL entry {dll!r}")
        add_symbols(exports, dll, symbols)
    if not any(exports.values()):
        raise CoverageError(f"{path}: baseline has no exports")
    return exports, provenance


def app_spec(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise CoverageError(f"--app must be NAME=PATH: {value}")
    name, path_text = value.split("=", 1)
    path = Path(path_text)
    if not name.strip() or not path.exists():
        raise CoverageError(f"invalid app name or path: {value}")
    return name.strip(), path


def app_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    files = sorted(
        item for item in path.rglob("*")
        if item.is_file() and item.suffix.lower() in {".exe", ".dll"}
    )
    if not files:
        raise CoverageError(f"{path}: no .exe or .dll files")
    return files


def ratio(numerator: int, denominator: int) -> float | None:
    return round(100 * numerator / denominator, 4) if denominator else None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_input_hashes(specs: Iterable[str]) -> dict[str, str]:
    """Record the exact source bytes behind declaration-only matches."""
    result: dict[str, str] = {}
    for spec in specs:
        _override, root = source_spec(spec)
        files = sorted(root.rglob("*")) if root.is_dir() else [root]
        for path in files:
            if path.is_file() and path.suffix.lower() in {".c", ".h", ".def"}:
                result[str(path.resolve())] = sha256_file(path)
    return result


def measure_file(path: Path, baseline: dict[str, set[str]],
                 wrapper_artifact: dict[str, set[str]],
                 wrapper_source: dict[str, set[str]],
                 kernelex_source: dict[str, set[str]] | None = None,
                 app_peers: dict[str, set[str]] | None = None) -> dict:
    imports = pe_imports(path)
    kernelex_source = kernelex_source or {}
    app_peers = app_peers or {}
    counts: Counter[str] = Counter()
    unresolved: Counter[tuple[str, str, str]] = Counter()
    for item in imports:
        dll, symbol = item["dll"], item["symbol"]
        if symbol in baseline.get(dll, set()):
            counts["baseline"] += 1
        elif symbol in app_peers.get(dll, set()):
            counts["app_peer"] += 1
        elif symbol in wrapper_artifact.get(dll, set()):
            counts["wrapper_artifact"] += 1
        elif symbol in kernelex_source.get(dll, set()):
            counts["kernelex_source"] += 1
        elif symbol in wrapper_source.get(dll, set()):
            counts["wrapper_source"] += 1
        else:
            counts["unresolved"] += 1
            unresolved[(dll, symbol, item["kind"])] += 1
        counts["total"] += 1
        counts[item["kind"]] += 1
    matched = counts["total"] - counts["unresolved"]
    non_source = counts["baseline"] + counts["wrapper_artifact"]
    return {
        "file": str(path.resolve()),
        "sha256": sha256_file(path),
        "counts": dict(counts),
        "matched": matched,
        "matched_percent": ratio(matched, counts["total"]),
        "baseline_or_artifact_percent": ratio(non_source, counts["total"]),
        "unresolved": [
            {"dll": dll, "symbol": symbol, "kind": kind, "count": count}
            for (dll, symbol, kind), count in sorted(unresolved.items())
        ],
    }


def aggregate(files: list[dict]) -> dict:
    counts: Counter[str] = Counter()
    unresolved: Counter[tuple[str, str, str]] = Counter()
    for item in files:
        counts.update(item["counts"])
        for missing in item["unresolved"]:
            unresolved[(missing["dll"], missing["symbol"],
                        missing["kind"])] += missing["count"]
    matched = counts["total"] - counts["unresolved"]
    non_source = counts["baseline"] + counts["wrapper_artifact"]
    return {
        "counts": dict(counts),
        "matched": matched,
        "matched_percent": ratio(matched, counts["total"]),
        "baseline_or_artifact_percent": ratio(non_source, counts["total"]),
        "unresolved": [
            {"dll": dll, "symbol": symbol, "kind": kind, "count": count}
            for (dll, symbol, kind), count in sorted(unresolved.items())
        ],
    }


def report_command(args: argparse.Namespace) -> None:
    baseline, provenance = load_manifest(args.baseline)
    kernelex_source: dict[str, set[str]] = {}
    kernelex_provenance: str | None = None
    if args.kernelex_source_manifest:
        kernelex_source, kernelex_provenance = load_manifest(
            args.kernelex_source_manifest)
    wrappers_from_pe = artifact_exports(args.wrapper_pe)
    wrappers_from_source = source_exports(args.wrapper_source) if args.wrapper_source else {}
    apps: list[dict] = []
    groups: dict[str, list[Path]] = {}
    labels: dict[str, str] = {}
    for value in args.app:
        name, path = app_spec(value)
        folded = name.casefold()
        if folded in labels:
            raise CoverageError(f"duplicate app label: {name}")
        labels[folded] = name
        groups[name] = app_files(path)
    for value in args.app_file:
        name, path = app_spec(value)
        if not path.is_file():
            raise CoverageError(f"--app-file requires a PE file: {path}")
        folded = name.casefold()
        actual_name = labels.setdefault(folded, name)
        groups.setdefault(actual_name, []).append(path)
    for path in args.targets:
        name = path.stem
        folded = name.casefold()
        if folded in labels:
            raise CoverageError(f"duplicate app label: {name}")
        labels[folded] = name
        groups[name] = app_files(path)
    if not groups:
        raise CoverageError("supply at least one target PE or --app NAME=PATH")
    peer_paths: dict[str, list[Path]] = {}
    for value in args.app_peer_pe:
        name, path = app_spec(value)
        actual_name = labels.get(name.casefold())
        if actual_name is None:
            raise CoverageError(f"--app-peer-pe names unknown app: {name}")
        if not path.is_file():
            raise CoverageError(f"--app-peer-pe requires a PE file: {path}")
        peer_paths.setdefault(actual_name, []).append(path)
    peer_evidence: dict[str, list[dict[str, str]]] = {}
    for name, paths in groups.items():
        unique_paths = {path.resolve() for path in paths}
        if len(unique_paths) != len(paths):
            raise CoverageError(f"{name}: duplicate PE file in app selection")
        peers: dict[str, set[str]] = {}
        peer_evidence[name] = []
        for peer in peer_paths.get(name, []):
            merge_symbols(peers, pe_exports(peer, peer.name))
            peer_evidence[name].append({"file": str(peer.resolve()),
                                        "sha256": sha256_file(peer)})
        files = [measure_file(file, baseline, wrappers_from_pe,
                              wrappers_from_source, kernelex_source, peers)
                 for file in paths]
        apps.append({"app": name, **aggregate(files), "files": files,
                     "app_peer_pe": peer_evidence[name]})
    weighted = aggregate([file for app in apps for file in app["files"]])
    report = {
        "metric": "declared_pe32_i386_import_name_match",
        "baseline_provenance": provenance,
        "baseline_manifest": str(args.baseline.resolve()),
        "baseline_manifest_sha256": sha256_file(args.baseline),
        "kernelex_source_manifest": (str(args.kernelex_source_manifest.resolve())
                                       if args.kernelex_source_manifest else None),
        "kernelex_source_manifest_sha256": (sha256_file(args.kernelex_source_manifest)
                                              if args.kernelex_source_manifest else None),
        "kernelex_source_provenance": kernelex_provenance,
        "wrapper_source_inputs": args.wrapper_source,
        "wrapper_source_file_sha256": source_input_hashes(args.wrapper_source),
        "wrapper_pe_inputs": args.wrapper_pe,
        "wrapper_pe_file_sha256": {
            str(source_spec(spec)[1].resolve()): sha256_file(source_spec(spec)[1])
            for spec in args.wrapper_pe
        },
        "weighted_by_import_occurrence": weighted,
        "apps": apps,
        "limitations": [
            "This is not coverage of the complete Windows API surface.",
            "Wrapper source entries are declarations, not verified guest exports.",
            "KernelEx source entries are declarations, not verified installed or enabled APIs.",
            "Static import names do not prove loader, API behavior, or app runtime compatibility.",
            "GetProcAddress, COM, WinRT, plugins, and runtime-loaded DLLs are not enumerated.",
            "App peer PE exports are only supplied-module names, not proof of load or behavior.",
        ],
    }
    if args.json_out:
        args.json_out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
    print("Static PE32 x86 import-name matches (not Windows API coverage)")
    print(f"Baseline: {provenance}")
    for app in apps:
        counts = app["counts"]
        percent = (f"{app['matched_percent']:.2f}%" if app["matched_percent"] is not None
                   else "N/A")
        non_source = (f"{app['baseline_or_artifact_percent']:.2f}%"
                      if app["baseline_or_artifact_percent"] is not None else "N/A")
        print(f"  {app['app']}: static names matched {app['matched']}/"
              f"{counts.get('total', 0)} ({percent}); baseline/artifact "
              f"{non_source}; app peer {counts.get('app_peer', 0)}; "
              f"KernelEx source {counts.get('kernelex_source', 0)}; "
              f"project source {counts.get('wrapper_source', 0)}; "
              f"unresolved {counts.get('unresolved', 0)}")
        for missing in app["unresolved"]:
            print(f"    {missing['dll']}!{missing['symbol']} "
                  f"[{missing['kind']}] x{missing['count']}")
    counts = weighted["counts"]
    percent = (f"{weighted['matched_percent']:.2f}%"
               if weighted["matched_percent"] is not None else "N/A")
    non_source = (f"{weighted['baseline_or_artifact_percent']:.2f}%"
                  if weighted["baseline_or_artifact_percent"] is not None else "N/A")
    print(f"Weighted: static names matched {weighted['matched']}/"
          f"{counts.get('total', 0)} ({percent}); baseline/artifact {non_source}; "
          f"baseline {counts.get('baseline', 0)}, "
          f"app peer {counts.get('app_peer', 0)}, "
          f"wrapper PE {counts.get('wrapper_artifact', 0)}, "
          f"KernelEx source {counts.get('kernelex_source', 0)}, "
          f"wrapper source {counts.get('wrapper_source', 0)}")


def manifest_command(args: argparse.Namespace) -> None:
    exports = artifact_exports(args.pe)
    if args.source:
        merge_symbols(exports, source_exports(args.source))
    if not exports:
        raise CoverageError("supply at least one PE export artifact or API-table source")
    write_manifest(args.out, exports, args.provenance)
    print(f"Wrote {args.out}: {len(exports)} DLLs, "
          f"{sum(map(len, exports.values()))} export identities")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    manifest = commands.add_parser("manifest", help="create an export manifest from PE/API tables")
    manifest.add_argument("--pe", action="append", default=[], metavar="[DLL=]FILE")
    manifest.add_argument("--source", action="append", default=[], metavar="[DLL=]PATH")
    manifest.add_argument("--provenance", required=True,
                          help="exact installed guest/version provenance")
    manifest.add_argument("--out", required=True, type=Path)
    manifest.set_defaults(action=manifest_command)
    report = commands.add_parser("report", help="match imports against baseline and wrappers")
    report.add_argument("--baseline", required=True, type=Path)
    report.add_argument("--kernelex-source-manifest", type=Path,
                        help="separate KernelEx API-table declarations, not guest proof")
    report.add_argument("--wrapper-pe", action="append", default=[], metavar="[DLL=]FILE")
    report.add_argument("--app-peer-pe", action="append", default=[], metavar="NAME=FILE",
                        help="PE exports shipped by one selected app (names only)")
    report.add_argument("--wrapper-source", action="append", default=[],
                        metavar="[DLL=]PATH")
    report.add_argument("--app", action="append", default=[], metavar="NAME=PATH",
                        help="one app PE file or directory, repeat for each app")
    report.add_argument("--app-file", action="append", default=[], metavar="NAME=FILE",
                        help="add one selected PE to an app; repeat NAME for several files")
    report.add_argument("--json-out", type=Path)
    report.add_argument("targets", nargs="*", type=Path,
                        help="individual PE32 x86 files; each becomes one app label")
    report.set_defaults(action=report_command)
    args = parser.parse_args(argv)
    try:
        args.action(args)
    except (CoverageError, PEError, OSError, ValueError) as error:
        print(f"coverage error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
