#!/usr/bin/env python3
"""Build a conservative, reproducible unified view of agent-history archives.

The machine-specific archive remains authoritative.  This tool creates only
identity, membership, relation, and prompt-reading views; it never rewrites or
deletes sanitized session mirrors.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable

VERSION = "conservative-unify-v1"
EXPECTED_FREEZE_FINGERPRINT = (
    "ab7742cd853842273462007d3c1fca638efe7f2f1dd604dc22cc4d7b76161802"
)
EXPECTED = {
    "archive_sessions": 867,
    "logical_sessions": 549,
    "full_session_prompts": 3394,
    "prompt_only": 1492,
    "same_native_session": 185,
    "exact_source_duplicate": 268,
    "prompt_only_candidate": 1912,
}


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"expected JSON object: {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    values = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, 1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid JSONL {path}:L{line_no}: {error}") from error
            if not isinstance(value, dict):
                raise ValueError(f"expected JSON object {path}:L{line_no}")
            values.append(value)
    return values


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def write_jsonl(path: Path, values: Iterable[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        for value in values:
            handle.write(json.dumps(value, ensure_ascii=False, sort_keys=True) + "\n")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def stable_id(prefix: str, *parts: str) -> str:
    material = "\0".join(parts).encode("utf-8")
    return f"{prefix}-{hashlib.sha256(material).hexdigest()[:24]}"


def provider(row: dict[str, Any]) -> str:
    archive_id = str(row["archive_id"])
    return "codex" if archive_id.startswith("codex-") else "claude-code"


def identity_key(row: dict[str, Any]) -> tuple[str, str]:
    return provider(row), str(row["session_id"])


def preferred_member(members: list[dict[str, Any]]) -> dict[str, Any]:
    def rank(row: dict[str, Any]) -> tuple[int, int, int, str]:
        normalized_b = int(
            row.get("source_machine") == "machine-b"
            and row.get("normalization") is not None
        )
        completeness = int((row.get("assistant_turn_count") or 0) > 0)
        event_weight = sum(
            int(row.get(field) or 0)
            for field in ("user_prompt_count", "assistant_turn_count", "tool_call_count")
        )
        return normalized_b, completeness, event_weight, str(row["archive_id"])

    return max(members, key=rank)


def prompt_fingerprint(row: dict[str, Any]) -> str:
    # Exact timestamp and text only.  Similar text or nearby times never merge.
    material = json.dumps(
        [row.get("timestamp"), row.get("prompt")],
        ensure_ascii=False,
        separators=(",", ":"),
    )
    return hashlib.sha256(material.encode("utf-8")).hexdigest()


def clear_generated_files(root: Path) -> None:
    targets = [
        root / "indexes" / "unified",
        root / "prompts" / "unified",
        root / "reports" / "unified",
        root / "manifests" / "unified",
    ]
    for directory in targets:
        directory.mkdir(parents=True, exist_ok=True)
        for path in directory.iterdir():
            if path.is_file():
                path.unlink()


def build(root: Path) -> dict[str, Any]:
    freeze_path = root / "reports" / "pre-unify-manifest.json"
    freeze = read_json(freeze_path)
    fingerprint = freeze.get("aggregate_fingerprint")
    if fingerprint != EXPECTED_FREEZE_FINGERPRINT:
        raise ValueError(
            "pre-unify fingerprint mismatch: "
            f"expected {EXPECTED_FREEZE_FINGERPRINT}, got {fingerprint}"
        )

    archive_rows = []
    for machine in ("machine-a", "machine-b"):
        archive_rows.extend(read_jsonl(root / "indexes" / machine / "sessions.jsonl"))
    if len(archive_rows) != EXPECTED["archive_sessions"]:
        raise ValueError(f"expected 867 archive sessions, got {len(archive_rows)}")
    archive_by_id = {str(row["archive_id"]): row for row in archive_rows}
    if len(archive_by_id) != len(archive_rows):
        raise ValueError("duplicate archive_id in machine indexes")

    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in archive_rows:
        grouped[identity_key(row)].append(row)

    logical_by_key = {
        key: stable_id("ls", "native-session-v1", key[0], key[1]) for key in grouped
    }
    logical_by_archive = {
        str(row["archive_id"]): logical_by_key[key]
        for key, members in grouped.items()
        for row in members
    }

    logical_rows = []
    membership_rows = []
    for key, members in grouped.items():
        members = sorted(members, key=lambda row: (str(row["source_machine"]), str(row["archive_id"])))
        logical_id = logical_by_key[key]
        preferred = preferred_member(members)
        starts = [str(row["start_time"]) for row in members if row.get("start_time")]
        ends = [str(row["end_time"]) for row in members if row.get("end_time")]
        machines = sorted({str(row["source_machine"]) for row in members})
        completeness = (
            "user_only"
            if all(int(row.get("assistant_turn_count") or 0) == 0 for row in members)
            else "complete"
        )
        logical_rows.append(
            {
                "logical_session_id": logical_id,
                "identity_contract": "provider-native-session-v1",
                "provider": key[0],
                "native_session_id": key[1],
                "evidence_scope": "full_session",
                "interaction_completeness": completeness,
                "source_machines": machines,
                "member_count": len(members),
                "member_archive_ids": [str(row["archive_id"]) for row in members],
                "preferred_archive_id": str(preferred["archive_id"]),
                "preferred_member_basis": (
                    "machine-b event-level normalized mirror"
                    if preferred.get("normalization") is not None
                    else "most complete archive member; display choice only"
                ),
                "start_time": min(starts) if starts else None,
                "end_time": max(ends) if ends else None,
            }
        )
        for row in members:
            membership_rows.append(
                {
                    "logical_session_id": logical_id,
                    "provider": key[0],
                    "native_session_id": key[1],
                    "source_machine": row["source_machine"],
                    "archive_id": row["archive_id"],
                    "sanitized_session_path": row["sanitized_session_path"],
                    "sanitized_sha256": row["sanitized_sha256"],
                    "is_preferred_member": row["archive_id"] == preferred["archive_id"],
                    "evidence_scope": "full_session",
                }
            )

    logical_rows.sort(key=lambda row: (row.get("start_time") or "", row["logical_session_id"]))
    membership_rows.sort(key=lambda row: (row["logical_session_id"], row["source_machine"], row["archive_id"]))

    relation_input = read_jsonl(root / "reports" / "pre-unify-duplicate-candidates.jsonl")
    relation_rows = []
    for number, row in enumerate(relation_input, 1):
        archives: set[str] = set()
        for field in ("left_archive_id", "right_archive_id", "full_archive_id", "machine_a_archive_id"):
            value = row.get(field)
            if isinstance(value, str):
                archives.add(value)
        for field in ("left_archive_ids", "right_archive_ids"):
            value = row.get(field)
            if isinstance(value, list):
                archives.update(str(item) for item in value)
        enriched = dict(row)
        enriched["relation_id"] = stable_id(
            "rel", "pre-unify-relation-v1", str(number), json.dumps(row, sort_keys=True, ensure_ascii=False)
        )
        enriched["member_archive_ids"] = sorted(archives)
        enriched["logical_session_ids"] = sorted(
            {logical_by_archive[item] for item in archives if item in logical_by_archive}
        )
        relation_rows.append(enriched)

    prompt_input = []
    for machine in ("machine-a", "machine-b"):
        prompt_input.extend(read_jsonl(root / "prompts" / machine / "prompts.jsonl"))
    if len(prompt_input) != EXPECTED["full_session_prompts"]:
        raise ValueError(f"expected 3394 full-session prompts, got {len(prompt_input)}")

    prompt_groups: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in prompt_input:
        archive_id = str(row["archive_id"])
        if archive_id not in logical_by_archive:
            raise ValueError(f"prompt references unknown archive: {archive_id}")
        key = (logical_by_archive[archive_id], prompt_fingerprint(row))
        prompt_groups[key].append(row)

    full_prompt_rows = []
    for (logical_id, fingerprint_value), members in prompt_groups.items():
        members = sorted(members, key=lambda row: (str(row["source_machine"]), str(row["archive_id"]), int(row.get("turn_index") or 0)))
        display = members[0]
        full_prompt_rows.append(
            {
                "unified_prompt_id": stable_id("up", "exact-prompt-v1", logical_id, fingerprint_value),
                "logical_session_id": logical_id,
                "evidence_scope": "full_session",
                "timestamp": display.get("timestamp"),
                "prompt": display.get("prompt"),
                "project": display.get("project"),
                "cwd": display.get("cwd"),
                "tags": sorted({tag for row in members for tag in row.get("tags", [])}),
                "member_count": len(members),
                "member_pointers": [
                    {
                        "source_machine": row["source_machine"],
                        "archive_id": row["archive_id"],
                        "turn_index": row.get("turn_index"),
                        "source_pointer": row["source_pointer"],
                        "source_sha256": row["source_sha256"],
                    }
                    for row in members
                ],
                "deduplication_basis": "same logical session plus exact timestamp and prompt text",
            }
        )
    full_prompt_rows.sort(key=lambda row: (row.get("timestamp") or "", row["logical_session_id"], row["unified_prompt_id"]))

    prompt_only_input = read_jsonl(root / "prompts" / "machine-b" / "global-history" / "prompts.jsonl")
    prompt_only_rows = []
    for row in prompt_only_input:
        output = dict(row)
        output["prompt_only_id"] = stable_id(
            "po",
            "prompt-only-v1",
            str(row.get("source_environment")),
            str(row.get("source_file")),
            str(row.get("source_record_index")),
            str(row.get("source_sha256")),
        )
        output["logical_session_id"] = None
        output["identity_status"] = "unbound; candidate relations do not establish identity"
        prompt_only_rows.append(output)
    prompt_only_rows.sort(key=lambda row: (row.get("timestamp") or "", row["prompt_only_id"]))

    relation_counts = Counter(str(row.get("relation_type")) for row in relation_rows)
    validation_errors = []
    actual = {
        "archive_sessions": len(archive_rows),
        "logical_sessions": len(logical_rows),
        "full_session_prompts": len(prompt_input),
        "unified_full_session_prompt_rows": len(full_prompt_rows),
        "prompt_only": len(prompt_only_rows),
        "same_native_session": relation_counts["same_native_session"],
        "exact_source_duplicate": relation_counts["exact_source_duplicate"],
        "prompt_only_candidate": relation_counts["prompt_only_candidate"],
    }
    for field, expected in EXPECTED.items():
        if actual[field] != expected:
            validation_errors.append(f"{field}: expected {expected}, got {actual[field]}")
    if len(membership_rows) != len(archive_rows):
        validation_errors.append("not every archive session has exactly one membership row")
    if len({row["archive_id"] for row in membership_rows}) != len(archive_rows):
        validation_errors.append("archive membership is not one-to-one")
    if sum(row["member_count"] for row in full_prompt_rows) != len(prompt_input):
        validation_errors.append("full-session prompt provenance count mismatch")
    unresolved_relation_archives = sorted(
        {
            archive_id
            for row in relation_rows
            for archive_id in row["member_archive_ids"]
            if archive_id not in archive_by_id
        }
    )
    # Prompt-only relations legitimately point to records rather than archives.
    hard_unresolved = [
        item
        for item in unresolved_relation_archives
        if item.startswith(("claude-", "codex-", "toyc-", "tinylibc-"))
    ]
    if hard_unresolved:
        validation_errors.append(f"relations reference unknown archives: {hard_unresolved[:5]}")

    clear_generated_files(root)
    write_jsonl(root / "indexes" / "unified" / "logical-sessions.jsonl", logical_rows)
    write_jsonl(root / "indexes" / "unified" / "archive-membership.jsonl", membership_rows)
    write_jsonl(root / "indexes" / "unified" / "relations.jsonl", relation_rows)
    write_jsonl(root / "prompts" / "unified" / "full-session-prompts.jsonl", full_prompt_rows)
    write_jsonl(root / "prompts" / "unified" / "prompt-only.jsonl", prompt_only_rows)

    conflicts = []
    write_jsonl(root / "reports" / "unified" / "conflicts.jsonl", conflicts)
    validation = {
        "schema_version": VERSION,
        "input_freeze_fingerprint": fingerprint,
        "counts": actual,
        "errors": validation_errors,
        "status": "PASS" if not validation_errors else "FAIL",
        "notes": [
            "Machine-specific archives remain authoritative and unchanged.",
            "Prompt folding requires one logical session plus exact timestamp and text.",
            "Prompt-only and auxiliary file-history evidence are not promoted to full sessions.",
        ],
    }
    write_json(root / "reports" / "unified" / "validation.json", validation)

    generated = [
        root / "indexes" / "unified" / "logical-sessions.jsonl",
        root / "indexes" / "unified" / "archive-membership.jsonl",
        root / "indexes" / "unified" / "relations.jsonl",
        root / "prompts" / "unified" / "full-session-prompts.jsonl",
        root / "prompts" / "unified" / "prompt-only.jsonl",
        root / "reports" / "unified" / "conflicts.jsonl",
        root / "reports" / "unified" / "validation.json",
    ]
    manifest = {
        "schema_version": VERSION,
        "input_freeze_fingerprint": fingerprint,
        "identity_contract": "provider-native-session-v1",
        "generated_files": [
            {"path": path.relative_to(root).as_posix(), "sha256": digest(path), "bytes": path.stat().st_size}
            for path in generated
        ],
        "excluded_from_unified_prompt_views": {
            "evidence_scope": "file_history_snapshot",
            "records": freeze["auxiliary_file_history_snapshot_records"],
            "reason": "auxiliary bookkeeping is not conversational evidence",
        },
    }
    write_json(root / "manifests" / "unified" / "manifest.json", manifest)

    report = f"""# Conservative Unify V1 report

- input freeze fingerprint: `{fingerprint}`
- schema: `{VERSION}`
- validation: `{'PASS' if not validation_errors else 'FAIL'}`
- archive session records preserved: `{len(archive_rows)}`
- logical full sessions: `{len(logical_rows)}`
- source full-session prompts represented: `{len(prompt_input)}`
- non-duplicated prompt reading rows: `{len(full_prompt_rows)}`
- prompt-only records kept separate: `{len(prompt_only_rows)}`
- auxiliary file-history snapshots excluded from conversational views: `{freeze['auxiliary_file_history_snapshot_records']}`

The unified corpus is a derived identity and reading view. Machine-specific indexes and sanitized
session mirrors remain authoritative. No source record was deleted, canonicalized, or rewritten.
`preferred_archive_id` is a display choice only. Prompt rows are folded only within one logical
session when timestamp and prompt text are exactly equal; all physical pointers remain attached.

Relation counts:

- `same_native_session`: `{relation_counts['same_native_session']}`
- `exact_source_duplicate`: `{relation_counts['exact_source_duplicate']}`
- `prompt_only_candidate`: `{relation_counts['prompt_only_candidate']}`

Git correlation, semantic session merging, development episodes, and AUTHOR/AGENT/JOINT
attribution remain outside Unify V1.
"""
    (root / "reports" / "unified" / "unify-report.md").write_text(report, encoding="utf-8")

    if validation_errors:
        raise ValueError("unified validation failed: " + "; ".join(validation_errors))
    return validation


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default="local/agent-history")
    args = parser.parse_args()
    result = build(Path(args.output))
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
