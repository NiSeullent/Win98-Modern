"""Join the entire candidate inventory to source declarations and batch work.

This is an auditable backlog, never a compatibility percentage. DLL/name joins
do not equate ABI, architecture, stub, forwarder or behavioral equivalence.
COM/WinRT identities and overloads remain separate; lexical names cannot attach
Win32 implementation evidence to them. SDK provenance is retained without
copying SDK header/IDL text into the publication artefact.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import json
import re
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

from measure_pe_coverage import c_table_exports, def_exports, load_manifest, normalize_dll

ROOT = Path(__file__).resolve().parents[1]


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def api_key(dll, name):
    return normalize_dll(dll), name


def validate_receipt(manifest, manifest_path, receipt_path, summary, root):
    receipt = read_json(receipt_path)
    if receipt.get("schema") != "w98mod.api-source-receipt.v1" or receipt.get("manifest_sha256") != digest(manifest_path):
        raise ValueError("source receipt schema/manifest mismatch")
    if summary.get("receipt_sha256") != digest(receipt_path):
        raise ValueError("source receipt checksum mismatch")
    archives = {item["id"]: item for item in receipt["archives"]}
    verified = {item["source"]: item for item in summary["verified_sources"]}
    expected = {item["id"] for item in manifest["sources"]}
    if len(verified) != len(summary["verified_sources"]) or set(verified) != expected | {"project"}:
        raise ValueError("verified source set mismatch")
    expected_archives = {item["id"] for item in manifest["sources"] if "archive_url" in item}
    if set(archives) != expected_archives or len(archives) != len(receipt["archives"]):
        raise ValueError("archive receipt set mismatch")
    for source in manifest["sources"]:
        item = verified[source["id"]]
        if item["revision"] != source["revision"]:
            raise ValueError("verified source revision mismatch")
        if "archive_url" in source:
            archive = archives[source["id"]]
            if archive["revision"] != source["revision"] or archive["url"] != source["archive_url"] or item["archive_sha256"] != archive["sha256"]:
                raise ValueError("archive identity mismatch")
            path = (root / archive["path"]).resolve()
            if not path.is_relative_to(root.resolve() / "build") or path.stat().st_size != archive["bytes"] or digest(path) != archive["sha256"]:
                raise ValueError("archive content mismatch")
        elif item.get("local_git_head_verified") is not True:
            raise ValueError("unverified local source")
    return digest(receipt_path)


def validate_upstream(manifest, manifest_path, upstream_path, summary, rows, root):
    if summary.get("schema") != "w98mod.upstream-exports.v1":
        raise ValueError("unexpected upstream summary schema")
    if summary.get("manifest_sha256") != digest(manifest_path):
        raise ValueError("upstream index was generated from a different source manifest")
    if summary.get("jsonl_sha256") != digest(upstream_path):
        raise ValueError("upstream index checksum mismatch")
    if summary.get("row_count") != len(rows):
        raise ValueError("upstream row count mismatch")
    expected = {item["id"]: item["revision"] for item in manifest["sources"]}
    current_hashes = {}
    for item in rows:
        required = ("source", "revision", "dll", "name", "kind", "target", "ordinal", "source_path", "line", "source_file_sha256")
        if any(key not in item for key in required):
            raise ValueError("incomplete source provenance in upstream index")
        if item["kind"] not in {"export_declaration", "stub", "forward", "data"}:
            raise ValueError("unrecognized source declaration kind")
        if not isinstance(item["line"], int) or item["line"] < 1 or not re.fullmatch(r"[a-f0-9]{64}", item["source_file_sha256"]):
            raise ValueError("invalid source line/hash")
        if item["source"] == "project":
            path = (root / item["source_path"]).resolve()
            if not path.is_relative_to(root.resolve() / "src"):
                raise ValueError("project index path outside src")
            if path not in current_hashes:
                current_hashes[path] = digest(path)
            if current_hashes[path] != item["source_file_sha256"]:
                raise ValueError("project source changed; rebuild upstream index")
        elif expected.get(item["source"]) != item["revision"]:
            raise ValueError("source revision does not match source manifest")


def read_project(root):
    """Read target DLL tables, including both M98_API and literal entry syntax."""
    output = defaultdict(list)
    inputs = {}
    for path in sorted((root / "src").glob("*")):
        if path.suffix not in {".c", ".def"}:
            continue
        content = path.read_text(encoding="utf-8-sig")
        # Preserve source line numbers while excluding commented-out declarations.
        parsed_content = re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
            lambda m: m.group() if m.group().startswith('"') else "\n" * m.group().count("\n"),
            content, flags=re.S)
        file_hash = digest(path)
        source_path = path.relative_to(root).as_posix()
        inputs[source_path] = file_hash
        exports = def_exports(path, None) if path.suffix == ".def" else c_table_exports(path, None)
        if path.suffix == ".c":
            arrays = dict(re.findall(r"\b(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", parsed_content, re.S))
            for dll, array in re.findall(r'\{\s*"([^"\r\n]+\.DLL)"\s*,\s*(\w+)\s*,', parsed_content, re.I):
                for name in re.findall(r'\{\s*"([^"\r\n]+)"\s*,', arrays.get(array, "")):
                    exports.setdefault(normalize_dll(dll), set()).add(name)
        for dll, names in sorted(exports.items()):
            for name in sorted(names):
                matching_lines = [n for n, line in enumerate((parsed_content if path.suffix == ".c" else content).splitlines(), 1)
                                  if (f'"{name}"' in line if path.suffix == ".c" else
                                      re.match(r"^\s*" + re.escape(name) + r"(?:\s|=|$)", line))]
                output[api_key(dll, name)].append({
                    "source_path": source_path, "lines": matching_lines,
                    "source_file_sha256": file_hash, "kind": "project_export_declaration",
                })
    return output, inputs


def validate_groups(groups):
    nodes = {x["id"]: x for x in groups["foundations"]}
    if len(nodes) != len(groups["foundations"]):
        raise ValueError("duplicate foundation ID")
    visiting, visited = set(), set()

    def visit(name):
        if name not in nodes:
            raise ValueError(f"unknown foundation {name}")
        if name in visiting:
            raise ValueError(f"dependency cycle at {name}")
        if name in visited:
            return
        visiting.add(name)
        for dep in nodes[name]["depends_on"]:
            visit(dep)
        visiting.remove(name)
        visited.add(name)
    for name in nodes:
        visit(name)
    rule_ids = set()
    for rule in groups["rules"]:
        if rule["id"] in rule_ids:
            raise ValueError("duplicate rule ID")
        rule_ids.add(rule["id"])
        for dep in rule["depends_on"]:
            visit(dep)
        for field in ("dll", "name"):
            if field in rule:
                re.compile(rule[field])


def assign_batch(row, groups):
    if row["category"].startswith("com_"):
        return {"id": "com:" + row["iid"], "title": "COM " + row.get("interface", row["iid"]),
                "depends_on": ["com-runtime"], "priority": 5}
    if row["category"].startswith("winrt_"):
        if not row.get("namespace") and row.get("iid"):
            return {"id": "winrt-iid:" + row["iid"], "title": "WinRT " + row.get("interface", row["iid"]),
                    "depends_on": ["winrt-runtime"], "priority": 6}
        namespace = row.get("namespace") or "unresolved"
        return {"id": "winrt:" + namespace, "title": namespace,
                "depends_on": ["winrt-runtime"], "priority": 6}
    for rule in groups["rules"]:
        if "categories" in rule and row["category"] not in rule["categories"]:
            continue
        if "dll" in rule and not re.search(rule["dll"], row.get("dll") or ""):
            continue
        if "name" in rule and not re.search(rule["name"], row.get("name") or ""):
            continue
        return {key: rule[key] for key in ("id", "title", "depends_on", "priority")}
    dll = row.get("dll") or "unresolved"
    return {"id": "dll:" + dll, "title": dll,
            "depends_on": ["abi-loader"], "priority": 5}


def merge_records(sdk_records, upstream, native, project, groups):
    indexed = defaultdict(list)
    for item in upstream:
        if not item.get("dll") or not item.get("name"):
            raise ValueError("upstream row without DLL/name")
        indexed[api_key(item["dll"], item["name"])].append(item)
    rows, seen_ids, sdk_keys = [], set(), set()
    for item in sdk_records:
        row = dict(item)
        if row["id"] in seen_ids:
            raise ValueError(f"duplicate SDK identity: {row['id']}")
        seen_ids.add(row["id"])
        row["in_sdk_inventory"] = True
        if row.get("dll") and row.get("name") and not row["category"].startswith(("com_", "winrt_")):
            row["dll"] = normalize_dll(row["dll"])
            sdk_keys.add(api_key(row["dll"], row["name"]))
        rows.append(row)
    native_keys = {(dll, name) for dll, names in native.items() for name in names}
    for dll, name in sorted((set(indexed) | native_keys | set(project)) - sdk_keys):
        ident = f"supplemental:{dll}!{name}"
        if ident in seen_ids:
            raise ValueError(f"identity collision: {ident}")
        seen_ids.add(ident)
        rows.append({"id": ident, "dll": dll, "name": name,
                     "category": "supplemental_export_declaration", "architecture": None,
                     "in_sdk_inventory": False})
    for row in rows:
        # No COM/WinRT lexical-name join, including inherited IUnknown methods.
        key = api_key(row["dll"], row["name"]) if row.get("dll") and row.get("name") and not row["category"].startswith(("com_", "winrt_")) else None
        declarations = sorted((x for x in indexed.get(key, []) if x["source"] != "project"), key=lambda x: (
            x["source"], x["source_path"], x["line"], x["kind"]))
        row["upstream_declarations"] = declarations
        row["upstream_sources"] = sorted({x["source"] for x in declarations})
        row["upstream_kinds"] = sorted({x["kind"] for x in declarations})
        row["native_export_present"] = key in native_keys
        row["project_declarations"] = project.get(key, [])
        row["implementation_status"] = "declared_in_project" if row["project_declarations"] else "listed"
        row["source_selection_status"] = "review_required" if declarations else "no_indexed_export_source"
        row["behavioral_coverage"] = "unassessed"
        row["full_compatibility_verified"] = False
        row["batch"] = assign_batch(row, groups)["id"]
    return sorted(rows, key=lambda x: x["id"])


def build_queue(rows, groups):
    batches = {}
    for row in rows:
        batch = assign_batch(row, groups)
        item = batches.setdefault(batch["id"], {**batch, "state": "awaiting_contract_review",
            "members": [], "categories": Counter(), "upstream_sources": Counter(),
            "project_declared": 0, "native_export_present": 0,
            "guest_subset_verified_candidates": 0, "historical_evidence_candidates": 0})
        item["members"].append(row["id"])
        item["categories"][row["category"]] += 1
        item["upstream_sources"].update(row["upstream_sources"])
        item["project_declared"] += bool(row["project_declarations"])
        item["native_export_present"] += row["native_export_present"]
        item["guest_subset_verified_candidates"] += row["behavioral_coverage"] == "guest_static_subset_verified"
        item["historical_evidence_candidates"] += row["behavioral_coverage"] == "historical_guest_subset_evidence"
    for item in batches.values():
        item["candidate_count"] = len(item["members"])
        if item["guest_subset_verified_candidates"]:
            item["state"] = "partial_guest_subset_evidence"
        elif item["historical_evidence_candidates"]:
            item["state"] = "historical_evidence_requires_revalidation"
        elif item["project_declared"]:
            item["state"] = "declarations_present_contract_review_required"
        item["required_gates"] = ["ABI_and_contract_review", "both_Wine_and_ReactOS_review",
            "license_and_dependencies", "family_implementation", "host_behavior_and_PE98",
            "direct_installed_guest", "guest_static_import", "application_regression"]
    return {"schema": "w98mod.porting-queue.v1", "foundations": groups["foundations"],
            "batches": sorted(batches.values(), key=lambda x: (x["priority"], x["id"]))}


def attach_evidence(rows, evidence, root):
    if evidence.get("schema") != "w98mod.api-guest-evidence.v1":
        raise ValueError("unexpected API evidence schema")
    by_api, identities, hash_cache = defaultdict(list), set(), {}

    def same_hash(relative, expected):
        path = (root / relative).resolve()
        if not path.is_relative_to(root.resolve()):
            raise ValueError("evidence path outside repository")
        if path not in hash_cache:
            hash_cache[path] = digest(path) if path.is_file() else None
        return hash_cache[path] == expected

    for item in evidence["records"]:
        if item["id"] in identities or item.get("full_compatibility_verified") is not False:
            raise ValueError("duplicate evidence or unsupported full-compatibility claim")
        identities.add(item["id"])
        receipt = item["receipt"]
        if receipt["exit_code"] != 0 or receipt["timed_out"] or receipt["output_truncated"]:
            raise ValueError("failed/incomplete test cannot provide positive behavioral evidence")
        output = receipt["output"].encode("utf-8")
        if len(output) != receipt["output_bytes"] or hashlib.sha256(output).hexdigest() != receipt["output_sha256"]:
            raise ValueError("guest output receipt mismatch")
        if not item["scope"] or not item["limitations"] or not item["source_hashes"] or not item["apis"]:
            raise ValueError("missing evidence scope, limitations or source identity")
        current = all(same_hash(path, sha) for path, sha in item["source_hashes"].items())
        current = same_hash(item["provider"]["path"], item["provider"]["sha256"]) and current
        current = same_hash(item["test"]["path"], item["test"]["sha256"]) and current
        current = all(same_hash(artifact["path"], artifact["sha256"])
                      for artifact in item.get("supporting_artifacts", [])) and current
        for api in item["apis"]:
            by_api[api_key(api["dll"], api["name"])].append({
                "id":item["id"], "matches_current_artifacts":current,
                "evidence_kind":item["evidence_kind"], "documentation":item["documentation"]})
    for row in rows:
        key = api_key(row["dll"], row["name"]) if row.get("dll") and row.get("name") and not row["category"].startswith(("com_", "winrt_")) else None
        row["behavioral_evidence"] = by_api.get(key, [])
        row["behavioral_evidence_ids"] = [item["id"] for item in row["behavioral_evidence"]]
        if row["behavioral_evidence"]:
            row["behavioral_coverage"] = "guest_static_subset_verified" if any(item["matches_current_artifacts"] for item in row["behavioral_evidence"]) else "historical_guest_subset_evidence"


def excel_safe(value):
    text = str(value) if value is not None else ""
    return "'" + text if text.startswith(("=", "+", "-", "@")) else text


def write_catalog(rows, output):
    jsonl = output / "catalog.jsonl.gz"
    with jsonl.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as stream:
        for row in rows:
            stream.write((json.dumps(row, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode())
    fields = ["id", "category", "dll", "name", "interface", "namespace", "owner", "method",
              "batch", "implementation_status", "source_selection_status", "native_export_present",
              "upstream_sources", "upstream_kinds", "behavioral_coverage", "behavioral_evidence_ids", "full_compatibility_verified"]
    with (output / "catalog.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: excel_safe(";".join(row[key]) if isinstance(row.get(key), list)
                                              else row.get(key, "")) for key in fields})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "porting/sources.json")
    parser.add_argument("--upstream", type=Path, default=ROOT / "build/api-catalog/upstream-exports.jsonl")
    parser.add_argument("--upstream-summary", type=Path, default=ROOT / "build/api-catalog/upstream-summary.json")
    parser.add_argument("--groups", type=Path, default=ROOT / "porting/groups.json")
    parser.add_argument("--receipt", type=Path, default=ROOT / "build/api-sources/receipt.json")
    parser.add_argument("--evidence", type=Path, default=ROOT / "benchmarks/api-guest-evidence-v1.json")
    parser.add_argument("--output", type=Path, default=ROOT / "build/api-catalog")
    parser.add_argument("--summary", type=Path, default=ROOT / "benchmarks/api-porting-catalog-v1.json")
    args = parser.parse_args()
    manifest = read_json(args.manifest)
    sdk_path = ROOT / manifest["sdk_inventory"]
    sdk_summary = read_json(ROOT / manifest["sdk_summary"])
    if digest(sdk_path) != sdk_summary["inventory_sha256"]:
        raise ValueError("SDK inventory does not match pinned summary")
    sdk = read_json(sdk_path)
    upstream_summary = read_json(args.upstream_summary)
    receipt_hash = validate_receipt(manifest, args.manifest, args.receipt, upstream_summary, ROOT)
    upstream = [json.loads(line) for line in args.upstream.read_text(encoding="utf-8").splitlines() if line]
    validate_upstream(manifest, args.manifest, args.upstream, upstream_summary, upstream, ROOT)
    groups = read_json(args.groups)
    validate_groups(groups)
    native_path = ROOT / "benchmarks/win98se-ko-oem-native-exports-v1.json"
    native, _ = load_manifest(native_path)
    project, project_inputs = read_project(ROOT)
    rows = merge_records(sdk["records"], upstream, native, project, groups)
    evidence = read_json(args.evidence)
    attach_evidence(rows, evidence, ROOT)
    queue = build_queue(rows, groups)
    assert sum(x["candidate_count"] for x in queue["batches"]) == len(rows)
    args.output.mkdir(parents=True, exist_ok=True)
    write_catalog(rows, args.output)
    write_json(args.output / "work-queue.json", queue)
    artifacts = {name: digest(args.output / name) for name in ("catalog.jsonl.gz", "catalog.csv", "work-queue.json")}
    summary = {
        "schema": "w98mod.porting-catalog-summary.v1",
        "status": "declaration inventory and complete candidate backlog; denominator and semantics unvalidated",
        "counts": {"catalog_records": len(rows), "sdk_candidates": len(sdk["records"]),
                   "supplemental_records": len(rows) - len(sdk["records"]),
                   "by_category": dict(sorted(Counter(x["category"] for x in rows).items())),
                   "with_upstream_declarations": sum(bool(x["upstream_declarations"]) for x in rows),
                   "with_native_export": sum(x["native_export_present"] for x in rows),
                   "with_project_declaration": sum(bool(x["project_declarations"]) for x in rows),
                   "with_current_guest_subset_evidence": sum(x["behavioral_coverage"] == "guest_static_subset_verified" for x in rows),
                   "with_historical_guest_subset_evidence": sum(x["behavioral_coverage"] == "historical_guest_subset_evidence" for x in rows),
                   "batches": len(queue["batches"]), "unassigned_records": 0},
        "compatibility_percentage": None,
        "evidence_policy": "Export declarations, native presence, stubs and forwarders do not demonstrate behavior. Linked guest receipts verify only stated contract subsets for pinned provider/test hashes; unassessed does not mean known-broken. Earlier tests not yet entered in the registry remain documented separately.",
        "inputs": {"sdk_inventory_sha256": digest(sdk_path),
                   "sources_manifest_sha256": digest(args.manifest), "groups_sha256": digest(args.groups),
                   "upstream_exports_sha256": digest(args.upstream),
                   "upstream_summary_sha256": digest(args.upstream_summary),
                   "source_receipt_sha256": receipt_hash,
                   "behavioral_evidence_sha256": digest(args.evidence),
                   "native_manifest_sha256": digest(native_path), "project_sources": project_inputs},
        "artifacts": artifacts,
        "behavioral_evidence": evidence,
        "upstream_summary": upstream_summary,
        "unresolved_sources": manifest["unresolved_sources"],
        "limitations": [
            "A declaration is not an implementation; exact ABI, behavior and dependencies require review.",
            "All SDK candidates and supplemental names are retained, including aliases, overloads, ordinal-only symbols, data and stubs. This union is not a validated Windows API denominator.",
            "Upstream architecture/conditional declarations are retained. Same-name matches do not establish an x86 implementation.",
            "COM and WinRT have no lexical Win32-name matches; IID/contract and ABI reviews remain required.",
            "NT/Unix host, security, kernel, service, COM/WinRT and graphics dependencies need backend work, not mechanical renaming.",
            "Default DLL batches still require detailed family subdivision; family assignment is a reviewable planning heuristic."
        ],
        "batch_summary_policy": "Named functional batches plus the 20 largest default batches; complete membership is in work-queue.json",
        "batches": [{key: value for key, value in item.items() if key != "members"}
                    for item in queue["batches"] if item["id"] in {r["id"] for r in groups["rules"]}],
        "largest_default_batches": [{key: value for key, value in item.items() if key != "members"}
            for item in sorted((b for b in queue["batches"] if b["id"] not in {r["id"] for r in groups["rules"]}),
                               key=lambda b: (-b["candidate_count"], b["id"]))[:20]],
    }
    write_json(args.summary, summary)
    write_json(args.output / "summary.json", summary)
    package = args.output / "api-porting-catalog.zip"
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in ("catalog.jsonl.gz", "catalog.csv", "work-queue.json", "summary.json"):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, (args.output / name).read_bytes())
    (args.output / "api-porting-catalog.zip.sha256").write_text(digest(package) + "  " + package.name + "\n", encoding="ascii")
    print(json.dumps({"catalog_records": len(rows), "sdk_candidates_retained": len(sdk["records"]),
                      "batches": len(queue["batches"]), "output": str(args.output),
                      "compatibility_percentage": None}, ensure_ascii=False))


if __name__ == "__main__":
    main()
