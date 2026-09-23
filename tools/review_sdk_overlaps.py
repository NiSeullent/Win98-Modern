"""Review name-only COM IDL / WinMD collisions in the pinned SDK inventory.

This is a bounded review of the 67 lexical candidates first found in SDK
10.0.28000.2705. A differing interface GUID rules out an identical interface
identity; equal names, argument counts, or projected signatures alone never
establish a semantic duplicate. No compatibility denominator is changed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import uuid
from collections import Counter, defaultdict
from pathlib import Path

from build_sdk_inventory import (INTERFACE_RE, UUID_RE, _compressed_uint,
                                 _preceding_attributes, matching_brace,
                                 method_name, strip_comments,
                                 top_level_statements)


VERSION = "10.0.28000.2705"
SCHEMA = "w98mod.sdk-idl-winmd-name-review.v1"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def lexical_pairs(records: list[dict]) -> list[tuple[dict, dict]]:
    winmd: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for record in records:
        if record["category"] == "winrt_metadata_method_candidate":
            winmd[(record["owner"], record["method"])].append(record)
    pairs = [(record, match)
             for record in records if record["category"] == "com_idl_method_candidate"
             for match in winmd.get((record["interface"], record["method"]), ())]
    return sorted(pairs, key=lambda pair: (pair[0]["id"], pair[1]["id"]))


def split_idl_parameters(source: str) -> list[str]:
    if not source.strip() or source.strip() == "void":
        return []
    result: list[str] = []
    start = 0
    parens = brackets = braces = 0
    for index, char in enumerate(source):
        if char == "(":
            parens += 1
        elif char == ")":
            parens -= 1
        elif char == "[":
            brackets += 1
        elif char == "]":
            brackets -= 1
        elif char == "{":
            braces += 1
        elif char == "}":
            braces -= 1
        elif char == "," and parens == brackets == braces == 0:
            result.append(source[start:index].strip())
            start = index + 1
    result.append(source[start:].strip())
    return result


def idl_declaration(source: str, iid: str, name: str, overload: int) -> dict | None:
    clean = strip_comments(source)
    clean = re.sub(r"(?m)^[ \t]*cpp_quote\([^\n]*\)[ \t]*$", "", clean)
    clean = re.sub(r"(?m)^[ \t]*\#[^\n]*$", "", clean)
    for match in INTERFACE_RE.finditer(clean):
        attributes = _preceding_attributes(clean, match.start())
        uuid_match = UUID_RE.search(attributes)
        if not uuid_match or uuid_match.group(1).lower() != iid:
            continue
        opening = match.end() - 1
        body = clean[opening + 1:matching_brace(clean, opening)]
        seen: Counter = Counter()
        for statement in top_level_statements(body):
            method = method_name(statement)
            if not method:
                continue
            seen[method] += 1
            if method != name or seen[method] != overload:
                continue
            signature_start = re.search(r"\b" + re.escape(name) + r"\s*\(", statement)
            if not signature_start:
                return None
            arguments = statement[signature_start.end():].strip()
            if not arguments.endswith(")"):
                return None
            params = split_idl_parameters(arguments[:-1])
            prefix = statement[:signature_start.start()]
            return {
                "declaration_sha256": hashlib.sha256(statement.encode("utf-8")).hexdigest(),
                "declaration_text": statement,
                "declared_return": "HRESULT" if re.search(r"\bHRESULT\s*$", prefix) else None,
                "parameter_count": len(params),
                "parameter_declarations": params,
                "retval_parameter_count": sum(bool(re.search(r"\bretval\b", p, re.I)) for p in params),
            }
    return None


def guid_attribute(blob: bytes) -> str | None:
    # Windows.Foundation.Metadata.GuidAttribute has a 16-byte fixed value in
    # mixed-endian GUID order, then a UInt16 count of named arguments.
    if len(blob) != 20 or blob[:2] != b"\x01\x00" or blob[18:] != b"\x00\x00":
        return None
    return str(uuid.UUID(bytes_le=blob[2:18]))


def attribute_type(attr) -> tuple[str, str] | None:
    constructor = attr.Type.row if attr.Type else None
    klass = constructor.Class.row if constructor and getattr(constructor, "Class", None) else None
    if klass is None:
        return None
    return str(getattr(klass, "TypeNamespace", "")), str(getattr(klass, "TypeName", ""))


def type_name(metadata, encoded: int) -> str | None:
    table_tag = encoded & 3
    row_id = encoded >> 2
    tables = {0: metadata.net.mdtables.TypeDef, 1: metadata.net.mdtables.TypeRef}
    table = tables.get(table_tag)
    if table is None or row_id < 1 or row_id > len(table.rows):
        return None
    row = table.rows[row_id - 1]
    return f"{row.TypeNamespace}.{row.TypeName}".lstrip(".")


PRIMITIVES = {
    0x01: "void", 0x02: "bool", 0x03: "char", 0x04: "int8", 0x05: "uint8",
    0x06: "int16", 0x07: "uint16", 0x08: "int32", 0x09: "uint32",
    0x0A: "int64", 0x0B: "uint64", 0x0C: "float32", 0x0D: "float64",
    0x0E: "string", 0x18: "native_int", 0x19: "native_uint", 0x1C: "object",
}


def signature_type(data: bytes, offset: int, metadata) -> tuple[str, int]:
    if offset >= len(data):
        raise ValueError("truncated signature type")
    kind = data[offset]
    offset += 1
    if kind in PRIMITIVES:
        return PRIMITIVES[kind], offset
    if kind in (0x11, 0x12):  # ValueType, Class
        token, offset = _compressed_uint(data, offset)
        return type_name(metadata, token) or f"unresolved_type_{token}", offset
    if kind == 0x15:  # GenericInst: ValueType/Class, type, argument count, arguments
        if offset >= len(data) or data[offset] not in (0x11, 0x12):
            raise ValueError("invalid generic instance type")
        generic_kind = data[offset]
        token, offset = _compressed_uint(data, offset + 1)
        argument_count, offset = _compressed_uint(data, offset)
        arguments = []
        for _ in range(argument_count):
            argument, offset = signature_type(data, offset, metadata)
            arguments.append(argument)
        owner = type_name(metadata, token) or f"unresolved_type_{token}"
        prefix = "valuetype" if generic_kind == 0x11 else "class"
        return f"{prefix} {owner}<{', '.join(arguments)}>", offset
    if kind in (0x0F, 0x10, 0x1D):  # Ptr, ByRef, SzArray
        nested, offset = signature_type(data, offset, metadata)
        return ({0x0F: "ptr", 0x10: "byref", 0x1D: "array"}[kind] + f"<{nested}>"), offset
    if kind in (0x13, 0x1E):  # VAR, MVAR
        index, offset = _compressed_uint(data, offset)
        return f"generic_{kind:02x}_{index}", offset
    if kind in (0x1F, 0x20):  # optional/required custom modifier
        _, offset = _compressed_uint(data, offset)
        return signature_type(data, offset, metadata)
    raise ValueError(f"unsupported signature element 0x{kind:02x}")


def parse_winmd_signature(blob: bytes, metadata) -> dict:
    result = {"blob_hex": blob.hex(), "decode_status": "unresolved"}
    try:
        if not blob:
            raise ValueError("empty method signature")
        flags = blob[0]
        offset = 1
        if flags & 0x10:  # generic method
            _, offset = _compressed_uint(blob, offset)
        parameter_count, offset = _compressed_uint(blob, offset)
        result["calling_convention_byte"] = flags
        result["parameter_count"] = parameter_count
        result["return_type"], offset = signature_type(blob, offset, metadata)
        params = []
        for _ in range(parameter_count):
            parameter, offset = signature_type(blob, offset, metadata)
            params.append(parameter)
        result["parameter_types"] = params
        if offset != len(blob):
            raise ValueError("unparsed trailing signature bytes")
        result["decode_status"] = "decoded_supported_elements"
    except (IndexError, ValueError) as exc:
        result["decode_note"] = str(exc)
    return result


def load_inputs(inventory_path: Path, summary_path: Path) -> tuple[list[tuple[dict, dict]], dict, dict]:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    if summary["sdk_version"] != VERSION or sha256(inventory_path) != summary["inventory_sha256"]:
        raise ValueError("inventory bytes/version differ from committed 28000 summary")
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    if inventory["sdk_version"] != VERSION or inventory["input_tree_sha256"] != summary["input_tree_sha256"]:
        raise ValueError("inventory source tree differs from committed 28000 summary")
    pairs = lexical_pairs(inventory["records"])
    return pairs, inventory["input_files_sha256"], summary


def idl_interface_iids(source: str) -> dict[str, str]:
    clean = strip_comments(source)
    result = {}
    for match in INTERFACE_RE.finditer(clean):
        uuid_match = UUID_RE.search(_preceding_attributes(clean, match.start()))
        if uuid_match:
            result[match.group("name")] = uuid_match.group(1).lower()
    return result


def referenced_idl_interfaces(declaration: dict, iids: dict[str, str]) -> list[dict]:
    result = []
    for parameter in declaration["parameter_declarations"]:
        if not re.search(r"\bretval\b", parameter, re.I):
            continue
        for name in re.findall(r"\bI[A-Za-z_]\w*\b", parameter):
            if name in iids:
                result.append({"name": name, "iid": iids[name]})
    return result


def review(inventory_path: Path, summary_path: Path, sdk_root: Path) -> dict:
    pairs, file_hashes, summary = load_inputs(inventory_path, summary_path)
    winmd_rel = f"UnionMetadata/{VERSION}/Windows.winmd"
    winmd_path = sdk_root / winmd_rel
    if sha256(winmd_path) != file_hashes[winmd_rel] or file_hashes[winmd_rel] != summary["winmd_sha256"]:
        raise ValueError("WinMD differs from pinned inventory input")

    import dnfile
    metadata = dnfile.dnPE(str(winmd_path))
    if not metadata.net or not metadata.net.mdtables.TypeDef:
        raise ValueError("not readable WinMD metadata")
    wanted = {(right["namespace"], right["owner"]) for _, right in pairs}
    types = {(str(row.TypeNamespace), str(row.TypeName)): (index, row)
             for index, row in enumerate(metadata.net.mdtables.TypeDef.rows, 1)
             if (str(row.TypeNamespace), str(row.TypeName)) in wanted}
    type_indices_by_name = {f"{row.TypeNamespace}.{row.TypeName}".lstrip("."): index
                            for index, row in enumerate(metadata.net.mdtables.TypeDef.rows, 1)}
    guids: dict[int, str] = {}
    for attr in metadata.net.mdtables.CustomAttribute.rows:
        parent = attr.Parent
        if not parent or parent.table.name != "TypeDef":
            continue
        if attribute_type(attr) == ("Windows.Foundation.Metadata", "GuidAttribute"):
            guid = guid_attribute(attr.Value.value)
            if guid is not None:
                guids[parent.row_index] = guid

    inherited_interfaces: dict[int, list[dict]] = defaultdict(list)
    for implementation in metadata.net.mdtables.InterfaceImpl.rows:
        if not implementation.Class or not implementation.Interface:
            continue
        interface = implementation.Interface.row
        if interface is None or not hasattr(interface, "TypeNamespace"):
            continue
        full_name = f"{interface.TypeNamespace}.{interface.TypeName}".lstrip(".")
        inherited_interfaces[implementation.Class.row_index].append({
            "name": full_name,
            "guid": guids.get(type_indices_by_name.get(full_name)),
        })

    idl_cache: dict[str, str] = {}
    idl_iid_cache: dict[str, dict[str, str]] = {}
    reviewed = []
    for left, right in pairs:
        sources = []
        for rel in left["sources"]:
            path = sdk_root / rel
            if sha256(path) != file_hashes[rel]:
                raise ValueError(f"IDL file differs from pinned inventory: {rel}")
            source = idl_cache.setdefault(rel, path.read_text(encoding="utf-8", errors="replace"))
            idl_iid_cache.setdefault(rel, idl_interface_iids(source))
            sources.append({"path": rel, "sha256": file_hashes[rel],
                            "declaration": idl_declaration(source, left["iid"], left["method"], left["overload"])})

        key = right["namespace"], right["owner"]
        type_index, type_row = types[key]
        seen: Counter = Counter()
        selected = None
        for method_ref in type_row.MethodList:
            method = method_ref.row
            if not method or not method.Flags.mdPublic:
                continue
            method_name_value = str(method.Name)
            seen[method_name_value] += 1
            if method_name_value == right["method"] and seen[method_name_value] == right["overload"]:
                selected = method_ref
                break
        if selected is None:
            raise ValueError(f"WinMD method not found: {right['id']}")
        guid = guids.get(type_index)
        state = ("distinct_interface_guid" if guid and guid != left["iid"] else
                 "matching_guid_requires_abi_review" if guid else "missing_winmd_guid")
        signature = parse_winmd_signature(selected.row.Signature.value, metadata)
        dependency_review = None
        if state == "distinct_interface_guid":
            reason = (f"IDL IID {left['iid']} differs from WinMD GuidAttribute {guid}; "
                      "these are distinct nominal interface identities.")
        elif state == "matching_guid_requires_abi_review":
            idl_base_iids = [{"path": source["path"], "base": left["base"],
                              "iid": idl_iid_cache[source["path"]].get(left["base"])}
                             for source in sources]
            idl_retval_iids = [{"path": source["path"], "interfaces":
                                referenced_idl_interfaces(source["declaration"],
                                                          idl_iid_cache[source["path"]])}
                               for source in sources]
            winmd_return = signature.get("return_type")
            dependency_review = {
                "idl_base_interfaces": idl_base_iids,
                "idl_retval_interfaces": idl_retval_iids,
                "winmd_inherited_interfaces": inherited_interfaces.get(type_index, []),
                "winmd_projected_return_interface": {
                    "name": winmd_return,
                    "guid": guids.get(type_indices_by_name.get(winmd_return)),
                },
            }
            reason = ("The nominal IID matches, but the IDL base/retval interface IIDs "
                      "differ from the WinMD projected references. The WinMD method "
                      "signature is a projection, so binary ABI and semantic equivalence "
                      "remain unresolved; do not deduplicate this pair.")
        else:
            reason = "No decodable WinMD GuidAttribute; interface identity remains unresolved."
        reviewed.append({
            "idl_id": left["id"], "winmd_id": right["id"],
            "lexical_key": {"unqualified_owner": left["interface"], "method": left["method"]},
            "idl": {"iid": left["iid"], "base": left["base"], "sources": sources},
            "winmd": {"namespace": right["namespace"], "interface_guid": guid,
                      "type_def_row": type_index, "method_def_row": selected.row_index,
                      "method_def_token": f"0x{(0x06000000 | selected.row_index):08x}",
                      "signature": signature,
                      "source": winmd_rel, "source_sha256": summary["winmd_sha256"]},
            "identity_review": state,
            "decision_reason": reason,
            "dependency_review": dependency_review,
        })
    counts = dict(sorted(Counter(row["identity_review"] for row in reviewed).items()))
    return {"schema": SCHEMA, "sdk_version": VERSION,
            "input_inventory_sha256": summary["inventory_sha256"],
            "input_tree_sha256": summary["input_tree_sha256"],
            "winmd_sha256": summary["winmd_sha256"],
            "matching_rule": "unqualified IDL interface name + method name equals WinMD owner name + method name",
            "scope_note": "Lexical subset only; no denominator or compatibility score is changed",
            "counts": {"lexical_pairs": len(reviewed), **counts}, "reviews": reviewed}


def self_test() -> None:
    blob = bytes.fromhex("0100bc481ddf87047f4e9d5ef09e77e412460000")
    assert guid_attribute(blob) == "df1d48bc-0487-4e7f-9d5e-f09e77e41246"
    assert guid_attribute(blob[:-1]) is None
    assert len(split_idl_parameters("[in] int a, [out, retval] int *b")) == 2
    assert len(split_idl_parameters("[in, size_is(2)] int *a")) == 1
    statement = "[propget] HRESULT Sample([in] int a, [out, retval] int *b)"
    source = "[uuid(0dc5e6ed-3e16-4bf1-8f9a-a979878bc195)] interface ITest : IUnknown {" + statement + ";};"
    result = idl_declaration(source, "0dc5e6ed-3e16-4bf1-8f9a-a979878bc195", "Sample", 1)
    assert result and result["declared_return"] == "HRESULT"
    assert result["parameter_count"] == 2 and result["retval_parameter_count"] == 1
    print("self-test: GUID blob, IDL parameter splitting, declaration extraction passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, default=Path("build/sdk-inventory-28000-candidates-v1.json"))
    parser.add_argument("--summary", type=Path, default=Path("benchmarks/sdk-inventory-28000-candidates-summary-v1.json"))
    parser.add_argument("--sdk-root", type=Path, default=Path("build/sdk-28000/materialized"))
    parser.add_argument("--out", type=Path, default=Path("benchmarks/sdk-overlap-review-v1.json"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    result = review(args.inventory, args.summary, args.sdk_root)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    data = (json.dumps(result, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")
    args.out.write_bytes(data)
    print(f"{args.out}: {result['counts']}; SHA-256 {hashlib.sha256(data).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
