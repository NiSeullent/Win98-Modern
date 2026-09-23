"""List catalogue rows without loading the entire inventory into memory."""
import argparse
import gzip
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=ROOT / "build/api-catalog/catalog.jsonl.gz")
    parser.add_argument("--dll", help="case-insensitive exact target DLL, e.g. KERNEL32.DLL")
    parser.add_argument("--batch", help="exact batch ID, e.g. threadpool")
    parser.add_argument("--source", help="source ID, e.g. wine or reactos")
    parser.add_argument("--category")
    parser.add_argument("--name", help="case-insensitive substring in API/method name")
    parser.add_argument("--details", action="store_true", help="emit complete provenance and declaration data")
    parser.add_argument("--limit", type=int, default=0, help="0 means every matching row")
    args = parser.parse_args()
    if args.limit < 0:
        parser.error("limit must be nonnegative")
    count = 0
    with gzip.open(args.catalog, "rt", encoding="utf-8") as stream:
        for line in stream:
            row = json.loads(line)
            if args.dll and (row.get("dll") or "").upper() != args.dll.upper():
                continue
            if args.batch and row["batch"] != args.batch:
                continue
            if args.source and args.source not in row["upstream_sources"]:
                continue
            if args.category and row["category"] != args.category:
                continue
            if args.name and args.name.casefold() not in (row.get("name") or row.get("method") or "").casefold():
                continue
            item = row if args.details else {key: row.get(key) for key in
                ("id", "batch", "upstream_sources", "upstream_kinds", "implementation_status", "behavioral_coverage")}
            print(json.dumps(item, ensure_ascii=False))
            count += 1
            if args.limit and count >= args.limit:
                break


if __name__ == "__main__":
    main()
