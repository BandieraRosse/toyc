#!/usr/bin/env python3
"""Verify a line-oriented agent-history archive without third-party packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

LEAK_RE = re.compile(
    r"(?i)(?:/home/(?!<)[^/\\\s\"']+|/mnt/[a-z]/Users/(?!<)[^/\\\s\"']+|"
    r"[A-Za-z]:[\\/]Users[\\/](?!<)[^\\/\s\"']+|"
    r"\b(?:sk|rk|pk)-[A-Za-z0-9_-]{12,}\b|"
    r"-----BEGIN [A-Z ]*PRIVATE KEY-----)"
)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def rows(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            yield number, json.loads(line)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="local/agent-history")
    parser.add_argument("--machine", "--source-machine", dest="source_machine", required=True)
    args = parser.parse_args()
    root = Path(args.output)
    session_index = root / "indexes" / args.source_machine / "sessions.jsonl"
    prompt_ledger = root / "prompts" / args.source_machine / "prompts.jsonl"
    links = root / "indexes" / args.source_machine / "git-links.jsonl"
    errors: list[str] = []
    session_map = {}
    session_hashes = {}
    session_count = 0
    for _, row in rows(session_index):
        session_count += 1
        path = root / row["sanitized_session_path"]
        if not path.is_file():
            errors.append(f"missing session {path}")
        elif digest(path) != row["sanitized_sha256"]:
            errors.append(f"session hash mismatch {path}")
        if path.is_file():
            session_hashes[path] = row["sanitized_sha256"]
        session_map[row["archive_id"]] = path
    prompt_rows = [row for _, row in rows(prompt_ledger)]
    prompt_count = len(prompt_rows)
    wanted: dict[Path, dict[int, list[dict]]] = {}
    for row in prompt_rows:
        relative, line_text = row["source_pointer"].split("#L", 1)
        path = root / relative
        wanted.setdefault(path, {}).setdefault(int(line_text), []).append(row)
    for path, line_rows in wanted.items():
        if not path.is_file():
            errors.extend(f"bad prompt pointer {row['source_pointer']}" for rows_at_line in line_rows.values() for row in rows_at_line)
            continue
        found = {}
        with path.open("r", encoding="utf-8") as handle:
            for no, line in enumerate(handle, 1):
                if no in line_rows:
                    found[no] = line
        for line_no, rows_at_line in line_rows.items():
            if line_no not in found:
                errors.extend(f"missing prompt line {row['source_pointer']}" for row in rows_at_line)
            for row in rows_at_line:
                if row["archive_id"] not in session_map:
                    errors.append(f"unknown prompt archive id {row['archive_id']}")
        expected_hash = rows_at_line[0]["source_sha256"]
        if session_hashes.get(path) != expected_hash:
            errors.append(f"prompt source hash mismatch {path}")
    link_count = sum(1 for _ in rows(links))
    leaked = []
    scan_roots = [
        root / "sanitized" / args.source_machine,
        root / "prompts" / args.source_machine,
        root / "indexes" / args.source_machine,
        root / "manifests" / args.source_machine,
        root / "reports" / args.source_machine,
    ]
    scan_files = (
        path
        for scan_root in scan_roots
        if scan_root.exists()
        for path in scan_root.rglob("*")
        if path.is_file() and path.suffix in {".jsonl", ".json", ".md"}
    )
    for path in scan_files:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for number, line in enumerate(handle, 1):
                if LEAK_RE.search(line):
                    leaked.append(f"{path}:L{number}")
                if path.suffix == ".jsonl":
                    try:
                        json.loads(line)
                    except json.JSONDecodeError:
                        errors.append(f"invalid JSONL {path}:L{number}")
    if leaked:
        errors.extend("possible private leak " + item for item in leaked[:20])
    result = {"sessions": session_count, "prompts": prompt_count, "git_links": link_count, "leak_hits": len(leaked), "errors": errors}
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
