#!/usr/bin/env python3
"""Build a portable, mechanically sanitized Claude Code history archive.

The archive format is intentionally line-oriented JSONL.  A second machine can
run the same command with different --source-root arguments and machine ID, then
merge the output files without a database or third-party dependency.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any, Iterable

VERSION = "machine-independent-v1"
TARGET_RE = re.compile(
    r"toyc|toy.?c|tinylibc|toycc|toy.?ccompiler|sc7|rasterfall|compiler",
    re.IGNORECASE,
)
SECRET_RE = re.compile(
    r"(?i)(bearer\s+)[A-Za-z0-9._~+/=-]+|"
    r"(api[_-]?key|access[_-]?token|secret|password|passwd|cookie|authorization)"
    r"(\s*[:=]\s*)[^\s,;]+|"
    r"\b(?:sk|rk|pk)-[A-Za-z0-9_-]{12,}\b|"
    r"-----BEGIN [A-Z ]*PRIVATE KEY-----.*?-----END [A-Z ]*PRIVATE KEY-----"
)
EMAIL_RE = re.compile(r"\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b")
PHONE_RE = re.compile(r"(?<![\d-])\+?(?:\d[ -]?){10,}(?![\d-])")
WIN_HOME_RE = re.compile(r"[A-Za-z]:[\\/]Users[\\/][^\\/\s\"']+", re.IGNORECASE)
WSL_HOME_RE = re.compile(r"/home/[^/\s\"']+")
MNT_HOME_RE = re.compile(r"/mnt/[a-z]/Users/[^/\s\"']+", re.IGNORECASE)
MNT_DRIVE_RE = re.compile(r"/mnt/[a-z]/[^/\s\"']+", re.IGNORECASE)
DRIVE_ABS_RE = re.compile(r"\b[A-Za-z]:[\\/][^\s\"'<>]+")
HEX_RE = re.compile(r"(?<![0-9a-f])[0-9a-f]{7,40}(?![0-9a-f])", re.IGNORECASE)
UUID_RE = re.compile(r"\b[0-9a-f]{8}-[0-9a-f-]{27,}\b", re.IGNORECASE)
EXTRA_REDACTIONS: list[str] = []


def now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sanitize_string(value: str) -> str:
    value = SECRET_RE.sub("<REDACTED_SECRET>", value)
    value = EMAIL_RE.sub("<REDACTED_PERSONAL_DATA>", value)
    value = PHONE_RE.sub("<REDACTED_PERSONAL_DATA>", value)
    value = MNT_HOME_RE.sub("<USER_HOME>", value)
    value = WIN_HOME_RE.sub("<USER_HOME>", value)
    value = WSL_HOME_RE.sub("<USER_HOME>", value)
    value = MNT_DRIVE_RE.sub("<PRIVATE_PATH>", value)
    value = DRIVE_ABS_RE.sub("<PRIVATE_PATH>", value)
    for private_text in EXTRA_REDACTIONS:
        value = value.replace(private_text, "<REDACTED_PERSONAL_DATA>")
    return value


def sanitize(value: Any) -> Any:
    if isinstance(value, str):
        return sanitize_string(value)
    if isinstance(value, list):
        return [sanitize(item) for item in value]
    if isinstance(value, dict):
        return {sanitize_string(key): sanitize(item) for key, item in value.items()}
    return value


def jsonl_records(path: Path) -> Iterable[tuple[int, dict[str, Any]]]:
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, 1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                yield line_no, value


def message_text(value: Any) -> str:
    chunks: list[str] = []
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        for key in ("content", "text", "input", "command", "stdout", "stderr"):
            if key in value:
                chunks.append(message_text(value[key]))
    elif isinstance(value, list):
        chunks.extend(message_text(item) for item in value)
    return "\n".join(item for item in chunks if item)


def event_role(record: dict[str, Any]) -> str | None:
    if record.get("type") == "assistant":
        return "assistant"
    if record.get("type") == "user":
        message = record.get("message")
        if isinstance(message, dict):
            content = message.get("content")
            if record.get("toolUseResult") is not None or (isinstance(content, list) and any(isinstance(item, dict) and item.get("type") == "tool_result" for item in content)):
                return None
        return "user"
    message = record.get("message")
    if isinstance(message, dict) and message.get("role") in ("user", "assistant"):
        if message.get("role") == "user" and (record.get("toolUseResult") is not None or (isinstance(message.get("content"), list) and any(isinstance(item, dict) and item.get("type") == "tool_result" for item in message["content"]))):
            return None
        return str(message["role"])
    return None


def event_timestamp(record: dict[str, Any]) -> str | None:
    candidates = [record.get("timestamp")]
    if isinstance(record.get("message"), dict):
        candidates.append(record["message"].get("timestamp"))
    for value in candidates:
        if isinstance(value, str) and value:
            return value
        if isinstance(value, (int, float)):
            return dt.datetime.fromtimestamp(value / 1000, dt.timezone.utc).isoformat().replace("+00:00", "Z")
    return None


def project_from_records(records: list[dict[str, Any]], fallback: str) -> str:
    for record in records:
        for key in ("cwd", "project"):
            if isinstance(record.get(key), str) and record[key]:
                return record[key]
        message = record.get("message")
        if isinstance(message, dict) and isinstance(message.get("cwd"), str):
            return message["cwd"]
    return fallback


def git_hashes(text: str) -> list[str]:
    text = UUID_RE.sub(" ", text)
    hashes: list[str] = []
    for match in HEX_RE.finditer(text):
        start = max(0, match.start() - 80)
        end = min(len(text), match.end() + 80)
        context = text[start:end].lower()
        if any(word in context for word in ("git", "commit", "head", "提交", "sha", "hash")):
            hashes.append(match.group(0))
    return sorted(set(hashes))


def git_head(text: str) -> str | None:
    for match in re.finditer(r"(?i)(?:rev-parse|head(?:\s|:|=))[^\n]{0,80}", text):
        found = re.search(r"\b[0-9a-f]{40}\b", match.group(0), re.IGNORECASE)
        if found:
            return found.group(0).lower()
    return None


def classify(text: str) -> list[str]:
    labels = []
    rules = {
        "IMPLEMENT": r"实现|修复|改成|增加|加入|编写|开发",
        "DEBUG": r"排查|bug|错误|崩溃|segfault|失败|问题",
        "TEST": r"测试|test|验证|回归|selfhost|自举",
        "DESIGN": r"方案|设计|架构|计划|如何|考虑",
        "CONSTRAINT": r"不要|必须|限制|依赖|标准库|standalone|独立",
        "DECISION": r"我决定|确定|选择|放弃|保留",
        "DOCUMENTATION": r"README|文档|说明|CLAUDE",
        "META": r"提交|commit|推送|仓库|git",
    }
    for label, pattern in rules.items():
        if re.search(pattern, text, re.IGNORECASE):
            labels.append(label)
    return labels or ["UNKNOWN"]


def source_files(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*.jsonl") if path.is_file())


def parse_source(path: Path) -> tuple[list[tuple[int, dict[str, Any]]], list[dict[str, Any]]]:
    pairs = list(jsonl_records(path))
    return pairs, [record for _, record in pairs]


def write_jsonl(path: Path, rows: Iterable[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


def run_archive(args: argparse.Namespace) -> None:
    output = Path(args.output)
    session_dir = output / "sanitized" / args.source_machine / "sessions"
    prompt_path = output / "prompts" / args.source_machine / "prompts.jsonl"
    session_index_path = output / "indexes" / args.source_machine / "sessions.jsonl"
    links_path = output / "indexes" / args.source_machine / "git-links.jsonl"
    session_dir.mkdir(parents=True, exist_ok=True)
    # The output is a reproducible generated mirror; remove only prior mirror
    # files in this exact machine-specific directory before rebuilding it.
    for old_file in session_dir.glob("*.jsonl"):
        old_file.unlink()

    roots = []
    for item in args.source_root:
        label, raw = item.split("=", 1)
        roots.append((label, Path(raw)))
    files: list[tuple[str, Path, Path]] = []
    for label, root in roots:
        files.extend((label, root, path) for path in source_files(root))

    manifest_rows = []
    prompt_rows = []
    session_rows = []
    link_rows = []
    all_secret_hits: list[str] = []
    total_raw = 0
    total_sanitized = 0

    for source_label, source_root, path in files:
        raw_hash = sha256(path)
        raw_size = path.stat().st_size
        total_raw += raw_size
        pairs, records = parse_source(path)
        if not records:
            continue
        session_id = str(records[0].get("sessionId") or path.stem)
        relative_source = path.relative_to(source_root).as_posix()
        source_suffix = hashlib.sha256(relative_source.encode("utf-8")).hexdigest()[:10]
        archive_id = f"{source_label}-{session_id}-{source_suffix}"
        project = sanitize_string(project_from_records(records, source_label))
        target_text = "\n".join(message_text(record) for record in records)
        # Explicit project roots are the inclusion boundary; keyword labels are
        # descriptive only and never used to discard a session.
        sanitized_path = session_dir / f"{archive_id}.jsonl"
        with sanitized_path.open("w", encoding="utf-8", newline="\n") as handle:
            for line_no, record in pairs:
                raw_line = json.dumps(record, ensure_ascii=False)
                if SECRET_RE.search(raw_line):
                    all_secret_hits.append(f"{source_label}/{path.name}:L{line_no}")
                handle.write(json.dumps(sanitize(record), ensure_ascii=False, sort_keys=True) + "\n")
        sanitized_hash = sha256(sanitized_path)
        sanitized_size = sanitized_path.stat().st_size
        total_sanitized += sanitized_size
        roles = [event_role(record) for record in records]
        timestamps = [event_timestamp(record) for record in records if event_timestamp(record)]
        user_prompts = []
        assistant_ids: set[str] = set()
        tool_count = 0
        for line_no, record in pairs:
            role = event_role(record)
            if role == "user":
                prompt = message_text(record.get("message", record.get("content", "")))
                user_prompts.append((line_no, event_timestamp(record), prompt))
                prompt_rows.append({
                    "source_machine": args.source_machine,
                    "session_id": session_id,
                    "archive_id": archive_id,
                    "turn_index": len(user_prompts) - 1,
                    "timestamp": event_timestamp(record),
                    "project": project,
                    "cwd": project,
                    "prompt": sanitize_string(prompt),
                    "source_pointer": f"sanitized/{args.source_machine}/sessions/{archive_id}.jsonl#L{line_no}",
                    "source_sha256": sanitized_hash,
                    "tags": classify(prompt),
                })
            elif role == "assistant":
                message = record.get("message")
                assistant_ids.add(str(message.get("id")) if isinstance(message, dict) and message.get("id") else f"line:{line_no}")
            if isinstance(record.get("message"), dict):
                content = record["message"].get("content")
                if isinstance(content, list):
                    tool_count += sum(1 for item in content if isinstance(item, dict) and item.get("type") == "tool_use")
        first_prompt = user_prompts[0][2] if user_prompts else None
        last_prompt = user_prompts[-1][2] if user_prompts else None
        hashes = git_hashes(target_text)
        starts = git_head(target_text)
        ends = starts
        session_rows.append({
            "source_machine": args.source_machine,
            "session_id": session_id,
            "archive_id": archive_id,
            "start_time": min(timestamps) if timestamps else None,
            "end_time": max(timestamps) if timestamps else None,
            "project": project,
            "cwd": project,
            "user_prompt_count": len(user_prompts),
            "assistant_turn_count": len(assistant_ids),
            "tool_call_count": tool_count,
            "first_prompt": sanitize_string(first_prompt) if first_prompt else None,
            "last_prompt": sanitize_string(last_prompt) if last_prompt else None,
            "keywords": sorted(set(re.findall(r"(?i)toyc|toy.?c|tinylibc|toycc|sc7|rasterfall|compiler", target_text))),
            "tags": classify(target_text),
            "starting_git_head": starts,
            "ending_git_head": ends,
            "commits_explicitly_created": [],
            "commits_observed": hashes,
            "historical_value": "candidate",
            "privacy_status": "sanitized; review required",
            "original_sha256": raw_hash,
            "sanitized_sha256": sanitized_hash,
            "sanitized_at": now_iso(),
            "sanitization_version": VERSION,
            "sanitized_session_path": f"sanitized/{args.source_machine}/sessions/{archive_id}.jsonl",
        })
        for commit in hashes:
            commit_context = re.search(rf"(?is).{{0,180}}{re.escape(commit)}.{{0,180}}", target_text)
            context = commit_context.group(0) if commit_context else ""
            direct = bool(re.search(r"git\s+commit|commit\s+(?:成功|完成|created)|提交成功|提交完成", context, re.IGNORECASE))
            link_rows.append({
                "source_machine": args.source_machine,
                "session_id": session_id,
                "archive_id": archive_id,
                "commit": commit,
                "evidence": "DIRECT" if direct else "POSSIBLE",
                "basis": "explicit commit operation and hash in same session context" if direct else "hash appears in git-related session context; no direct commit proof",
                "source_pointer": f"sanitized/{args.source_machine}/sessions/{archive_id}.jsonl",
            })

        manifest_rows.append({
            "source_machine": args.source_machine,
            "source_label": source_label,
            "source_path": f"<CLAUDE_DATA>/{source_label}/{path.name}",
            "source_type": "claude-code-session-jsonl",
            "session_id": session_id,
            "archive_id": archive_id,
            "original_size": raw_size,
            "original_sha256": raw_hash,
            "sanitized_size": sanitized_size,
            "sanitized_sha256": sanitized_hash,
            "sanitization_version": VERSION,
        })

    write_jsonl(prompt_path, sorted(prompt_rows, key=lambda row: (row["timestamp"] or "", row["session_id"], row["turn_index"])))
    write_jsonl(session_index_path, sorted(session_rows, key=lambda row: (row["start_time"] or "", row["session_id"])))
    write_jsonl(links_path, sorted(link_rows, key=lambda row: (row["commit"], row["session_id"])))
    manifest = output / "manifests" / args.source_machine / "source-files.md"
    manifest.parent.mkdir(parents=True, exist_ok=True)
    with manifest.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# {args.source_machine} Claude Code history manifest\n\n")
        handle.write(f"- source machine: `{args.source_machine}`\n- export time: `{now_iso()}`\n")
        handle.write(f"- sanitization version: `{VERSION}`\n- raw files: `{len(manifest_rows)}`\n")
        handle.write(f"- raw bytes: `{total_raw}`\n- sanitized bytes: `{total_sanitized}`\n")
        all_times = [row["start_time"] for row in session_rows if row["start_time"]] + [row["end_time"] for row in session_rows if row["end_time"]]
        if all_times:
            handle.write(f"- session time range: `{min(all_times)}` to `{max(all_times)}`\n")
        handle.write(f"- secret-pattern hits before replacement: `{len(all_secret_hits)}`\n")
        if args.prompt_index:
            index_path = Path(args.prompt_index)
            handle.write(f"- prompt index: `<CLAUDE_DATA>/{index_path.name}`; bytes `{index_path.stat().st_size}`; SHA256 `{sha256(index_path)}`\n")
        handle.write("- source paths are represented by neutral labels; exact private paths remain outside the repository.\n\n")
        handle.write("## Format\n\nEach source file is Claude Code event JSONL. The sanitized mirror preserves one JSON object per input line and recursively sanitizes string values. The prompt ledger points to the corresponding sanitized event line. SHA256 values refer to the original private file and repository mirror respectively.\n\n")
        handle.write("## Files\n\n")
        for row in manifest_rows:
            handle.write("| `{session_id}` | `{source_label}` | {original_size} | `{original_sha256}` | {sanitized_size} | `{sanitized_sha256}` |\n".format(**row))
    report = output / "reports" / args.source_machine / "archive-run.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps({"source_machine": args.source_machine, "generated_at": now_iso(), "raw_files": len(manifest_rows), "raw_bytes": total_raw, "sanitized_bytes": total_sanitized, "secret_pattern_hits": len(all_secret_hits), "secret_hit_pointers": all_secret_hits}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"raw_files": len(manifest_rows), "sessions": len(session_rows), "prompts": len(prompt_rows), "raw_bytes": total_raw, "sanitized_bytes": total_sanitized, "secret_pattern_hits": len(all_secret_hits)}, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--machine", "--source-machine", dest="source_machine", required=True)
    parser.add_argument("--output", default="local/agent-history")
    parser.add_argument("--prompt-index", help="optional Claude history.jsonl included in the source manifest")
    parser.add_argument("--source-root", action="append", required=True, metavar="LABEL=PATH", help="repeatable, explicit source root")
    parser.add_argument("--redact-string", action="append", default=[], help="repeatable machine-local literal to redact; never stored in the archive")
    args = parser.parse_args()
    EXTRA_REDACTIONS.extend(item for item in args.redact_string if item)
    run_archive(args)


if __name__ == "__main__":
    main()
