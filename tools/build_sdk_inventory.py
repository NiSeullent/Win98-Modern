"""Build a provisional, names-only Windows SDK API inventory.

Inputs are x86 UM COFF import libraries, UM/shared headers, UM IDL, and
UnionMetadata Windows.winmd.
No SDK source text is copied to the output. This is a candidate inventory, not a
Windows API denominator or a compatibility measurement.

WinRT metadata reading requires ``python -m pip install dnfile==0.18.0``.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path


SCHEMA = "w98mod.sdk-candidate-inventory.v1"
SUMMARY_SCHEMA = "w98mod.sdk-candidate-summary.v1"
TARGET_SDK = "10.0.28000.2705"
ARCH = "x86"
IMPORT_HEADER = struct.Struct("<HHHHIIHH")
INTERFACE_RE = re.compile(
    r"\b(?P<kind>interface|dispinterface)\s+(?P<name>[A-Za-z_]\w*)"
    r"\s*(?::\s*(?P<base>[A-Za-z_]\w*))?\s*\{", re.IGNORECASE
)
UUID_RE = re.compile(r"\buuid\s*\(\s*([0-9a-fA-F-]{36})\s*\)", re.IGNORECASE)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_hash(files: dict[str, str]) -> str:
    """Hash relative file names and their SHA-256s without machine paths."""
    digest = hashlib.sha256()
    for path, file_hash in sorted(files.items()):
        digest.update(path.encode("utf-8") + b"\0" + file_hash.encode("ascii") + b"\n")
    return digest.hexdigest()


def _undecorate(name: str) -> str:
    if name.startswith(("_", "@")):
        name = name[1:]
    return re.sub(r"@\d+$", "", name)


def parse_import_archive(path: Path) -> tuple[list[dict], Counter]:
    """Read short COFF import objects, not ordinary static-library objects."""
    data = path.read_bytes()
    if not data.startswith(b"!<arch>\n"):
        raise ValueError(f"not a COFF archive: {path}")
    offset = 8
    entries: list[dict] = []
    stats: Counter = Counter()
    while offset < len(data):
        if offset + 60 > len(data):
            raise ValueError(f"truncated archive header: {path}")
        header = data[offset : offset + 60]
        if header[58:60] != b"`\n":
            raise ValueError(f"invalid archive member header: {path}")
        try:
            size = int(header[48:58].strip())
        except ValueError as exc:
            raise ValueError(f"invalid archive member size: {path}") from exc
        start = offset + 60
        end = start + size
        if end > len(data):
            raise ValueError(f"truncated archive member: {path}")
        member = data[start:end]
        offset = end + (size & 1)
        stats["archive_members"] += 1
        if len(member) < IMPORT_HEADER.size or member[:4] != b"\0\0\xff\xff":
            stats["non_short_import_members"] += 1
            continue
        _, _, _, machine, _, payload_size, ordinal_hint, type_info = IMPORT_HEADER.unpack_from(member)
        stats["short_import_members"] += 1
        if machine != 0x14C:
            stats["non_x86_import_members"] += 1
            continue
        if payload_size > len(member) - IMPORT_HEADER.size:
            raise ValueError(f"truncated import payload: {path}")
        fields = member[IMPORT_HEADER.size : IMPORT_HEADER.size + payload_size].split(b"\0")
        if len(fields) < 3:
            raise ValueError(f"missing import symbol or DLL: {path}")
        symbol = fields[0].decode("utf-8", "replace")
        dll = fields[1].decode("utf-8", "replace")
        import_type = type_info & 3  # 0 code, 1 data, 2 const
        name_type = (type_info >> 2) & 7
        if import_type != 0:
            stats["data_or_const_import_members"] += 1
            continue
        if name_type == 0:
            stats["ordinal_import_members"] += 1
            continue
        if name_type == 1:
            export = symbol
        elif name_type == 2:
            export = symbol[1:]
        elif name_type == 3:
            export = _undecorate(symbol)
        elif name_type == 4 and len(fields) > 3:
            export = fields[2].decode("utf-8", "replace")
        else:
            stats["unsupported_name_type_members"] += 1
            continue
        if not dll or not export:
            stats["empty_name_members"] += 1
            continue
        entries.append({"dll": dll.upper(), "name": export, "symbol": symbol,
                        "ordinal_hint": ordinal_hint, "name_type": name_type})
        stats["named_code_import_members"] += 1
    if offset != len(data):
        raise ValueError(f"archive padding mismatch: {path}")
    return entries, stats


def strip_comments(source: str) -> str:
    """Remove C/IDL comments while retaining quoted strings and newlines."""
    out: list[str] = []
    index = 0
    quote = ""
    while index < len(source):
        char = source[index]
        nxt = source[index + 1] if index + 1 < len(source) else ""
        if quote:
            out.append(char)
            if char == "\\" and nxt:
                out.append(nxt)
                index += 2
                continue
            if char == quote:
                quote = ""
        elif char in ('"', "'"):
            quote = char
            out.append(char)
        elif char == "/" and nxt == "/":
            index += 2
            while index < len(source) and source[index] != "\n":
                index += 1
            continue
        elif char == "/" and nxt == "*":
            index += 2
            while index + 1 < len(source) and source[index : index + 2] != "*/":
                if source[index] == "\n":
                    out.append("\n")
                index += 1
            index += 2
            continue
        else:
            out.append(char)
        index += 1
    return "".join(out)


def matching_brace(source: str, opening: int) -> int:
    depth = 0
    quote = ""
    index = opening
    while index < len(source):
        char = source[index]
        if quote:
            if char == "\\":
                index += 2
                continue
            if char == quote:
                quote = ""
        elif char in ('"', "'"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise ValueError("unclosed IDL interface brace")


def _preceding_attributes(source: str, start: int) -> str:
    index = start - 1
    while index >= 0 and source[index].isspace():
        index -= 1
    if index < 0 or source[index] != "]":
        return ""
    end = index + 1
    depth = 0
    while index >= 0:
        if source[index] == "]":
            depth += 1
        elif source[index] == "[":
            depth -= 1
            if depth == 0:
                return source[index:end]
        index -= 1
    return ""


def top_level_statements(body: str) -> list[str]:
    statements: list[str] = []
    start = 0
    braces = brackets = parens = 0
    quote = ""
    index = 0
    while index < len(body):
        char = body[index]
        if quote:
            if char == "\\":
                index += 2
                continue
            if char == quote:
                quote = ""
        elif char in ('"', "'"):
            quote = char
        elif char == "{":
            braces += 1
        elif char == "}":
            braces -= 1
        elif char == "[":
            brackets += 1
        elif char == "]":
            brackets -= 1
        elif char == "(":
            parens += 1
        elif char == ")":
            parens -= 1
        elif char == ";" and braces == brackets == parens == 0:
            statements.append(body[start:index].strip())
            start = index + 1
        index += 1
    return statements


def method_name(statement: str) -> str | None:
    if re.match(r"^(?:typedef|enum|struct|union|import|cpp_quote|const|#)\b", statement):
        return None
    brackets = 0
    for index, char in enumerate(statement):
        if char == "[":
            brackets += 1
        elif char == "]":
            brackets -= 1
        elif char == "(" and brackets == 0:
            prefix = statement[:index]
            match = re.search(r"([A-Za-z_]\w*)\s*$", prefix)
            if not match or not prefix[: match.start()].strip():
                return None
            name = match.group(1)
            return name if name not in {"if", "switch", "sizeof"} else None
    return None


def parse_idl(source: str) -> tuple[list[dict], int]:
    clean = strip_comments(source)
    # MIDL's cpp_quote lines contain C/C++ text, sometimes with semicolons and
    # braces. They are not IDL declarations and can mask the next real method.
    clean = re.sub(r"(?m)^[ \t]*cpp_quote\([^\n]*\)[ \t]*$", "", clean)
    clean = re.sub(r"(?m)^[ \t]*\#[^\n]*$", "", clean)
    methods: list[dict] = []
    interface_count = 0
    for match in INTERFACE_RE.finditer(clean):
        attributes = _preceding_attributes(clean, match.start())
        uuid_match = UUID_RE.search(attributes)
        # UUID marks a COM identity; method-bearing unmarked RPC interfaces are excluded.
        if not uuid_match:
            continue
        opening = match.end() - 1
        body = clean[opening + 1 : matching_brace(clean, opening)]
        interface_count += 1
        occurrence: Counter = Counter()
        for statement in top_level_statements(body):
            name = method_name(statement)
            if not name:
                continue
            occurrence[name] += 1
            methods.append({"interface": match.group("name"), "base": match.group("base"),
                            "iid": uuid_match.group(1).lower(), "method": name,
                            "overload": occurrence[name], "kind": match.group("kind").lower()})
    return methods, interface_count


def _compressed_uint(data: bytes, offset: int) -> tuple[int, int]:
    first = data[offset]
    if first < 0x80:
        return first, offset + 1
    if first < 0xC0:
        return ((first & 0x3F) << 8) | data[offset + 1], offset + 2
    return ((first & 0x1F) << 24) | int.from_bytes(data[offset + 1 : offset + 4], "big"), offset + 4


def contract_version(blob: bytes) -> tuple[str | None, int | None]:
    """Decode WinRT ContractVersionAttribute's string/uint32 or uint32 form."""
    if len(blob) < 8 or blob[:2] != b"\x01\x00":
        return None, None
    if len(blob) == 8:
        return None, int.from_bytes(blob[2:6], "little")
    try:
        length, offset = _compressed_uint(blob, 2)
        if offset + length + 6 <= len(blob):
            name = blob[offset : offset + length].decode("utf-8")
            version = int.from_bytes(blob[offset + length : offset + length + 4], "little")
            return name, version
    except (IndexError, UnicodeDecodeError):
        pass
    return None, None


def _winrt_attributes(metadata) -> dict[tuple[str, int], tuple[str | None, int | None]]:
    versions: dict[tuple[str, int], tuple[str | None, int | None]] = {}
    table = metadata.net.mdtables.CustomAttribute
    if table is None:
        return versions
    for attr in table.rows:
        parent = attr.Parent
        if not parent or parent.table.name not in {"TypeDef", "MethodDef"}:
            continue
        constructor = attr.Type.row if attr.Type else None
        attr_type = constructor.Class.row if constructor and hasattr(constructor, "Class") and constructor.Class else None
        if (not attr_type or str(getattr(attr_type, "TypeNamespace", "")) != "Windows.Foundation.Metadata"
                or str(getattr(attr_type, "TypeName", "")) != "ContractVersionAttribute"):
            continue
        versions[(parent.table.name, parent.row_index)] = contract_version(attr.Value.value)
    return versions


def parse_winmd(path: Path) -> tuple[list[dict], Counter]:
    try:
        import dnfile
    except ImportError as exc:
        raise RuntimeError("WinRT metadata needs: python -m pip install dnfile==0.18.0") from exc
    metadata = dnfile.dnPE(str(path))
    if not metadata.net or not metadata.net.mdtables.TypeDef:
        raise ValueError(f"not .NET/WinRT metadata: {path}")
    versions = _winrt_attributes(metadata)
    result: list[dict] = []
    stats: Counter = Counter()
    for type_index, type_row in enumerate(metadata.net.mdtables.TypeDef.rows, 1):
        namespace = str(type_row.TypeNamespace)
        if (not namespace.startswith("Windows.") or not type_row.Flags.tdPublic
                or not type_row.Flags.tdWindowsRuntime):
            continue
        owner = str(type_row.TypeName)
        if owner == "<Module>":
            continue
        stats["public_winrt_types"] += 1
        owner_kind = "interface" if type_row.Flags.tdInterface else "class_or_delegate"
        type_contract = versions.get(("TypeDef", type_index), (None, None))
        overloads: Counter = Counter()
        for method_index in type_row.MethodList:
            method = method_index.row
            if not method or not method.Flags.mdPublic:
                continue
            name = str(method.Name)
            overloads[name] += 1
            contract, version = versions.get(("MethodDef", method_index.row_index), type_contract)
            result.append({"namespace": namespace, "owner": owner, "owner_kind": owner_kind,
                           "method": name, "overload": overloads[name],
                           "contract": contract,
                           "contract_version": (f"{version >> 16}.{version & 0xffff}"
                                                if version is not None else None)})
            stats["public_winrt_methods"] += 1
            if version is None:
                stats["methods_without_contract_version"] += 1
    return result, stats


def build(sdk_root: Path, sdk_version: str) -> tuple[dict, dict]:
    include = sdk_root / "Include" / sdk_version
    libs_dir = sdk_root / "Lib" / sdk_version / "um" / ARCH
    idl_dir = include / "um"
    winmd = sdk_root / "UnionMetadata" / sdk_version / "Windows.winmd"
    libs = sorted(libs_dir.glob("*.lib"), key=lambda p: p.name.lower())
    idls = sorted(idl_dir.rglob("*.idl"), key=lambda p: p.relative_to(include).as_posix().lower())
    if not libs or not idls or not winmd.is_file():
        raise FileNotFoundError("SDK x86 UM import libs, UM IDL, or UnionMetadata Windows.winmd missing")

    files: dict[str, str] = {}
    by_export: dict[tuple[str, str], dict] = {}
    lib_stats: Counter = Counter()
    for path in libs:
        rel = f"Lib/{sdk_version}/um/x86/{path.name}"
        files[rel] = sha256(path)
        entries, stats = parse_import_archive(path)
        lib_stats.update(stats)
        for entry in entries:
            key = entry["dll"], entry["name"]
            record = by_export.setdefault(key, {
                "id": f"win32:{entry['dll']}!{entry['name']}",
                "category": ("win32_api_set_alias" if entry["dll"].startswith(("API-MS-", "EXT-MS-"))
                             else "win32_named_export_candidate"),
                "architecture": ARCH, "dll": entry["dll"], "name": entry["name"],
                "source_kind": "sdk_x86_um_import_library", "sources": [],
                "minimum_os": None, "feature_dependencies": None,
            })
            record["sources"].append(rel)
    exports = list(by_export.values())
    for record in exports:
        record["sources"] = sorted(set(record["sources"]))

    # A lexical header hit is supporting provenance, not proof that the token
    # is a public callable declaration under the chosen preprocessor defines.
    by_name: dict[str, list[dict]] = defaultdict(list)
    for record in exports:
        record["header_call_token_sources"] = []
        by_name[record["name"]].append(record)
    headers = sorted([*include.joinpath("um").rglob("*.h"),
                      *include.joinpath("shared").rglob("*.h")],
                     key=lambda p: p.relative_to(include).as_posix().lower())
    call_re = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
    for path in headers:
        rel = f"Include/{sdk_version}/{path.relative_to(include).as_posix()}"
        files[rel] = sha256(path)
        source = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for name in {match.group(1) for match in call_re.finditer(source)}:
            for record in by_name.get(name, ()):
                record["header_call_token_sources"].append(rel)

    by_com: dict[str, dict] = {}
    com_interface_count = 0
    idl_entries: list[tuple[dict, str]] = []
    base_by_name: dict[str, str | None] = {}
    for path in idls:
        rel = f"Include/{sdk_version}/{path.relative_to(include).as_posix()}"
        files[rel] = sha256(path)
        source = path.read_text(encoding="utf-8", errors="replace")
        methods, interfaces = parse_idl(source)
        com_interface_count += interfaces
        for entry in methods:
            idl_entries.append((entry, rel))
            base_by_name[entry["interface"]] = entry["base"]

    def inspectable_derived(name: str, base: str | None) -> bool:
        seen = {name}
        while base and base not in seen:
            if base == "IInspectable":
                return True
            seen.add(base)
            base = base_by_name.get(base)
        return False

    for entry, rel in idl_entries:
        iid = entry["iid"]
        api_id = f"com:{{{iid}}}::{entry['method']}#{entry['overload']}"
        category = ("winrt_idl_method_candidate" if inspectable_derived(entry["interface"], entry["base"])
                    else "com_idl_method_candidate")
        record = by_com.setdefault(api_id, {
            "id": api_id, "category": category,
            "interface": entry["interface"], "iid": iid, "base": entry["base"],
            "method": entry["method"], "overload": entry["overload"],
            "architecture": None, "source_kind": "sdk_um_idl", "sources": [],
            "minimum_os": None, "feature_dependencies": None,
        })
        record["sources"].append(rel)
    com = list(by_com.values())
    for record in com:
        record["sources"] = sorted(set(record["sources"]))

    winmd_rel = f"UnionMetadata/{sdk_version}/Windows.winmd"
    files[winmd_rel] = sha256(winmd)
    winrt_raw, winrt_stats = parse_winmd(winmd)
    winrt: list[dict] = []
    for entry in winrt_raw:
        api_id = (f"winrt:{entry['namespace']}.{entry['owner']}::"
                  f"{entry['method']}#{entry['overload']}")
        winrt.append({"id": api_id, "category": "winrt_metadata_method_candidate",
                      "architecture": None, "source_kind": "sdk_union_winmd",
                      "sources": [winmd_rel], "minimum_os": None,
                      "feature_dependencies": None, **entry})

    records = sorted(exports + com + winrt, key=lambda x: x["id"])
    counts = dict(sorted(Counter(record["category"] for record in records).items()))
    status = ("provisional 26100 SDK source subset; not the full 28000 target denominator"
              if sdk_version == "10.0.26100.0" else
              f"provisional {sdk_version} SDK candidate pass; denominator policy pending")
    inventory = {
        "schema": SCHEMA, "sdk_version": sdk_version, "target_sdk_version": TARGET_SDK,
        "status": status,
        "counting_unit": "named x86 import-library code export or declared COM/WinRT method; overloads separate",
        "input_tree_sha256": tree_hash(files),
        "input_files_sha256": dict(sorted(files.items())),
        "records": records,
    }
    summary = {
        "schema": SUMMARY_SCHEMA, "sdk_version": sdk_version,
        "target_sdk_version": TARGET_SDK,
        "status": inventory["status"],
        "counting_unit": inventory["counting_unit"],
        "counts": {"total_candidates": len(records), **counts},
        "source_counts": {"x86_um_import_libraries": len(libs), "um_shared_header_files": len(headers),
                          "named_export_candidates_with_header_call_token":
                              sum(bool(record["header_call_token_sources"]) for record in exports),
                          "um_idl_files": len(idls),
                          "uuid_idl_interface_definitions": com_interface_count,
                          "winmd_files": 1, **dict(sorted(lib_stats.items())),
                          **dict(sorted(winrt_stats.items()))},
        "input_tree_sha256": inventory["input_tree_sha256"],
        "winmd_sha256": files[winmd_rel],
        "limitations": [
            (f"SDK {sdk_version} is older than frozen target {TARGET_SDK}."
             if sdk_version == "10.0.26100.0" else
             f"SDK {sdk_version} source coverage and denominator policy remain unvalidated."),
            "Short COFF import objects exclude static members and may include undocumented exports or aliases.",
            "IDL scan includes UUID-bearing UM interface definitions only; WinRT-style IDL can overlap WinMD.",
            "Header call-token matches are lexical evidence, not validated declarations.",
            "WinRT scan includes public Windows.* runtime type methods in aggregate Windows.winmd.",
            "Minimum OS and feature dependencies are unknown; no guest behavior has been tested.",
            "These counts are neither a complete Windows API denominator nor a compatibility numerator.",
        ],
    }
    return inventory, summary


def write_json(path: Path, value: dict) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = (json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + "\n").encode("utf-8")
    path.write_bytes(data)
    return hashlib.sha256(data).hexdigest()


def self_test() -> None:
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "test.lib"
        def archive_member(payload: bytes, type_info: int) -> bytes:
            member = IMPORT_HEADER.pack(0, 0xffff, 0, 0x14c, 0, len(payload), 7, type_info) + payload
            header = b"test.obj/".ljust(16) + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6)
            header += b"100644".ljust(8) + str(len(member)).encode().ljust(10) + b"`\n"
            return header + member + (b"\n" if len(member) & 1 else b"")
        path.write_bytes(b"!<arch>\n" + b"".join([
            archive_member(b"_CreateThing@4\0TEST.dll\0", 0x0c),  # undecorate
            archive_member(b"?Decorated@@\0TEST.dll\0", 0x04),  # exact name
            archive_member(b"_Prefix\0TEST.dll\0", 0x08),  # strip prefix
            archive_member(b"_Alias\0TEST.dll\0RealAlias\0", 0x10),  # export-as
            archive_member(b"_Ordinal\0TEST.dll\0", 0x00),  # ordinal-only
            archive_member(b"_Data\0TEST.dll\0", 0x05),  # data import
        ]))
        entries, counts = parse_import_archive(path)
        assert counts["named_code_import_members"] == 4
        assert counts["ordinal_import_members"] == counts["data_or_const_import_members"] == 1
        assert [entry["name"] for entry in entries] == ["CreateThing", "?Decorated@@", "Prefix", "RealAlias"]
        assert all(entry["dll"] == "TEST.DLL" for entry in entries)
    source = '''// interface IFake : IUnknown { HRESULT Wrong(); }
    [uuid(42f85136-db7e-439c-85f1-e4075d135fc8), object]
    interface IExample : IUnknown {
      typedef struct { int x; } THING;
      cpp_quote("virtual HRESULT Hidden();")
      #else
      HRESULT One([in, size_is(2)] int *values);
      [id(1)] HRESULT Two();
    };'''
    methods, interfaces = parse_idl(source)
    assert interfaces == 1 and [m["method"] for m in methods] == ["One", "Two"]
    assert contract_version(b"\x01\x00\x03Foo\x00\x00\x02\x00\x00\x00") == ("Foo", 131072)
    assert contract_version(b"\x01\x00\x00\x00\x01\x00\x00\x00") == (None, 65536)
    print("self-test: COFF import kinds/names, IDL comments/nesting/methods, contract blobs passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_out = Path("build/sdk-inventory-26100-provisional-v1.json")
    default_summary = Path("benchmarks/sdk-inventory-26100-provisional-summary-v1.json")
    parser.add_argument("--sdk-root", type=Path,
                        default=Path(r"C:\Program Files (x86)\Windows Kits\10"))
    parser.add_argument("--sdk-version", default="10.0.26100.0")
    parser.add_argument("--out", type=Path, default=default_out)
    parser.add_argument("--summary-out", type=Path, default=default_summary)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.sdk_version != "10.0.26100.0" and (args.out == default_out or args.summary_out == default_summary):
        parser.error("use version-specific --out and --summary-out paths for another SDK version")
    inventory, summary = build(args.sdk_root, args.sdk_version)
    inventory_hash = write_json(args.out, inventory)
    summary["inventory_sha256"] = inventory_hash
    write_json(args.summary_out, summary)
    print(f"{args.out}: {len(inventory['records'])} provisional {args.sdk_version} candidates")
    print(f"{args.summary_out}: {summary['counts']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
