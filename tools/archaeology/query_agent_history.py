#!/usr/bin/env python3
"""Read-only queries over the Conservative Unify V1 agent-history view."""

from __future__ import annotations

import argparse
import json
import signal
import sys
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Iterable


FULL_PROMPT_FILE = Path("prompts/unified/full-session-prompts.jsonl")
PROMPT_ONLY_FILE = Path("prompts/unified/prompt-only.jsonl")
LOGICAL_FILE = Path("indexes/unified/logical-sessions.jsonl")
MEMBERSHIP_FILE = Path("indexes/unified/archive-membership.jsonl")
RELATIONS_FILE = Path("indexes/unified/relations.jsonl")
VALIDATION_FILE = Path("reports/unified/validation.json")
MANIFEST_FILE = Path("manifests/unified/manifest.json")

SKIP_EVENT_TYPES = {"mode", "permission-mode", "ai-title", "last-prompt", "attachment"}
PROVIDER_ALIASES = {
    "claude": "claude-code",
    "claude-code": "claude-code",
    "codex": "codex",
}


class QueryError(Exception):
    """An expected, user-facing query or corpus error."""


def read_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except OSError as error:
        raise QueryError(f"cannot read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise QueryError(f"invalid JSON {path}:L{error.lineno}: {error.msg}") from error
    if not isinstance(value, dict):
        raise QueryError(f"expected JSON object: {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    try:
        handle = path.open("r", encoding="utf-8")
    except OSError as error:
        raise QueryError(f"cannot read {path}: {error}") from error
    with handle:
        for line_no, line in enumerate(handle, 1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise QueryError(f"invalid JSONL {path}:L{line_no}: {error.msg}") from error
            if not isinstance(value, dict):
                raise QueryError(f"expected JSON object {path}:L{line_no}")
            rows.append(value)
    return rows


def parse_timestamp(value: Any, *, field: str = "timestamp") -> datetime | None:
    if value in (None, ""):
        return None
    text = str(value)
    normalized = text[:-1] + "+00:00" if text.endswith("Z") else text
    try:
        result = datetime.fromisoformat(normalized)
    except ValueError as error:
        raise QueryError(f"invalid {field} timestamp: {text}") from error
    if result.tzinfo is None:
        result = result.replace(tzinfo=timezone.utc)
    return result.astimezone(timezone.utc)


def date_bound(value: str, *, end: bool) -> datetime:
    try:
        day = datetime.strptime(value, "%Y-%m-%d").replace(tzinfo=timezone.utc)
    except ValueError as error:
        raise QueryError(f"date must use YYYY-MM-DD: {value}") from error
    return day + timedelta(days=1) if end else day


def provider_filter(value: str | None) -> str | None:
    if value is None:
        return None
    normalized = PROVIDER_ALIASES.get(value.casefold())
    if normalized is None:
        raise QueryError("--provider must be claude or codex")
    return normalized


def safe_text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if isinstance(value, (int, float, bool)):
        return str(value)
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def compact_text(value: Any, limit: int = 280) -> str:
    text = " ".join(safe_text(value).split())
    if len(text) <= limit:
        return text
    return text[: limit - 1].rstrip() + "…"


def snippet(text: str, needle: str, limit: int = 320) -> str:
    folded = text.casefold()
    index = folded.find(needle.casefold())
    if index < 0:
        return compact_text(text, limit)
    start = max(0, index - limit // 3)
    end = min(len(text), start + limit)
    result = text[start:end]
    if start:
        result = "…" + result
    if end < len(text):
        result += "…"
    return " ".join(result.split())


def unique_join(values: Iterable[Any]) -> str:
    result = sorted({str(value) for value in values if value not in (None, "")})
    return ", ".join(result) if result else "-"


def in_time_range(value: Any, since: datetime | None, until: datetime | None) -> bool:
    timestamp = parse_timestamp(value)
    if timestamp is None:
        return since is None and until is None
    return (since is None or timestamp >= since) and (until is None or timestamp < until)


class Corpus:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.logical = read_jsonl(self.root / LOGICAL_FILE)
        self.membership = read_jsonl(self.root / MEMBERSHIP_FILE)
        self.relations = read_jsonl(self.root / RELATIONS_FILE)
        self.full_prompts = read_jsonl(self.root / FULL_PROMPT_FILE)
        self.prompt_only = read_jsonl(self.root / PROMPT_ONLY_FILE)
        self.validation = read_json(self.root / VALIDATION_FILE)
        self.manifest = read_json(self.root / MANIFEST_FILE)

        self.logical_by_id = {str(row["logical_session_id"]): row for row in self.logical}
        if len(self.logical_by_id) != len(self.logical):
            raise QueryError("duplicate logical_session_id in unified index")
        self.members_by_logical: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for row in self.membership:
            self.members_by_logical[str(row["logical_session_id"])].append(row)
        self.prompts_by_logical: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for row in self.full_prompts:
            self.prompts_by_logical[str(row["logical_session_id"])].append(row)

    def logical_context(self, logical_id: str) -> dict[str, str]:
        rows = self.prompts_by_logical.get(logical_id, [])
        return {
            "project": unique_join(row.get("project") for row in rows),
            "cwd": unique_join(row.get("cwd") for row in rows),
        }

    def matches_common(
        self,
        row: dict[str, Any],
        *,
        provider: str | None,
        project: str | None,
        since: datetime | None,
        until: datetime | None,
    ) -> bool:
        if provider and str(row.get("provider")) != provider:
            return False
        if not in_time_range(row.get("timestamp"), since, until):
            return False
        if project:
            haystack = f"{row.get('project', '')} {row.get('cwd', '')}".casefold()
            if project.casefold() not in haystack:
                return False
        return True

    def session_matches(
        self,
        row: dict[str, Any],
        *,
        provider: str | None,
        project: str | None,
        since: datetime | None,
        until: datetime | None,
    ) -> bool:
        if provider and str(row.get("provider")) != provider:
            return False
        start = parse_timestamp(row.get("start_time"), field="start_time")
        end = parse_timestamp(row.get("end_time"), field="end_time") or start
        if since and end and end < since:
            return False
        if until and start and start >= until:
            return False
        context = self.logical_context(str(row["logical_session_id"]))
        if project and project.casefold() not in f"{context['project']} {context['cwd']}".casefold():
            return False
        return True

    def preferred_members(self, logical_id: str) -> list[dict[str, Any]]:
        members = self.members_by_logical.get(logical_id, [])
        if not members:
            raise QueryError(f"logical session has no archive membership: {logical_id}")
        preferred = [row for row in members if row.get("is_preferred_member")]
        return preferred[:1] or members[:1]

    def transcript_path(self, member: dict[str, Any]) -> Path:
        relative = Path(str(member["sanitized_session_path"]))
        sanitized_root = (self.root / "sanitized").resolve()
        path = (self.root / relative).resolve()
        if not str(path).startswith(str(sanitized_root) + "/"):
            raise QueryError(f"refusing non-sanitized session path: {relative}")
        return path

    def transcript(self, logical_id: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
        member = self.preferred_members(logical_id)[0]
        path = self.transcript_path(member)
        return read_jsonl(path), member


def content_parts(value: Any) -> list[tuple[str, str]]:
    if isinstance(value, str):
        return [("text", value)] if value else []
    if isinstance(value, list):
        parts: list[tuple[str, str]] = []
        for item in value:
            if isinstance(item, dict):
                item_type = str(item.get("type", "text"))
                if item_type in {"text", "thinking"}:
                    text = item.get("text", item.get("thinking", ""))
                    if text:
                        parts.append((item_type, safe_text(text)))
                elif item_type in {"tool_use", "tool_result"}:
                    parts.append((item_type, safe_text(item)))
                elif item.get("content"):
                    parts.append((item_type, safe_text(item.get("content"))))
            elif item:
                parts.append(("text", safe_text(item)))
        return parts
    if value:
        return [("text", safe_text(value))]
    return []


def event_lines(event: dict[str, Any]) -> list[tuple[str, str, str]]:
    event_type = str(event.get("type", "event"))
    if event_type in SKIP_EVENT_TYPES:
        return []
    timestamp = safe_text(event.get("timestamp"))
    message = event.get("message")
    content = message.get("content") if isinstance(message, dict) else event.get("content")
    if content is None:
        content = event.get("summary", event.get("progress"))
    parts = content_parts(content)
    if not parts:
        for key in ("error", "formatted", "text"):
            if event.get(key):
                parts = [(key, safe_text(event[key]))]
                break
    if not parts:
        return []
    lines: list[tuple[str, str, str]] = []
    for part_type, part in parts:
        label = event_type.upper()
        if part_type == "tool_use":
            label = "TOOL"
        elif part_type == "tool_result":
            label = "TOOL RESULT"
        elif part_type == "thinking":
            label = "ASSISTANT THINKING"
        lines.append((timestamp, label, part))
    return lines


def print_text_results(rows: list[dict[str, Any]], needle: str) -> None:
    for row in rows:
        print(f"{row['logical_session_id']}  {row.get('timestamp', '-')}")
        print(f"  provider: {row.get('provider', '-')}  project/cwd: {row.get('project', '-') or '-'} / {row.get('cwd', '-') or '-'}")
        print(f"  {snippet(str(row.get('prompt', '')), needle)}")


def list_session_rows(corpus: Corpus, args: argparse.Namespace) -> list[dict[str, Any]]:
    rows = []
    for row in corpus.logical:
        if not corpus.session_matches(
            row,
            provider=provider_filter(args.provider),
            project=args.project,
            since=args.since,
            until=args.until,
        ):
            continue
        context = corpus.logical_context(str(row["logical_session_id"]))
        output = dict(row)
        output.update(
            {
                "project": context["project"],
                "cwd": context["cwd"],
                "prompt_count": len(corpus.prompts_by_logical.get(str(row["logical_session_id"]), [])),
                "archive_member_count": len(corpus.members_by_logical.get(str(row["logical_session_id"]), [])),
            }
        )
        rows.append(output)
    rows.sort(key=lambda row: (row.get("start_time") or "", row["logical_session_id"]))
    return rows


def render_session(corpus: Corpus, row: dict[str, Any], args: argparse.Namespace) -> dict[str, Any]:
    logical_id = str(row["logical_session_id"])
    context = corpus.logical_context(logical_id)
    events, preferred = corpus.transcript(logical_id)
    members = corpus.members_by_logical[logical_id]
    relation_rows = [
        relation
        for relation in corpus.relations
        if logical_id in [str(item) for item in relation.get("logical_session_ids", [])]
    ]
    conversation = []
    for event in events:
        for timestamp, label, text in event_lines(event):
            conversation.append({"timestamp": timestamp, "type": label, "text": text})

    result = {
        "session": {
            "logical_session_id": logical_id,
            "provider": row.get("provider"),
            "native_session_id": row.get("native_session_id"),
            "start_time": row.get("start_time"),
            "end_time": row.get("end_time"),
            "project": context["project"],
            "cwd": context["cwd"],
        },
        "archive_members": members,
        "preferred_archive_id": preferred.get("archive_id"),
        "conversation": conversation,
    }
    if args.provenance:
        result["relations"] = relation_rows
    return result


def print_session(data: dict[str, Any], provenance: bool) -> None:
    session = data["session"]
    print("SESSION")
    for label, key in (("logical_session_id", "logical_session_id"), ("provider", "provider"), ("native_session_id", "native_session_id"), ("start", "start_time"), ("end", "end_time"), ("cwd/project", "cwd")):
        value = session.get(key, "-")
        if key == "cwd":
            value = f"{session.get('cwd', '-')} / {session.get('project', '-')}"
        print(f"{label}: {value}")
    print("archive members:")
    for member in data["archive_members"]:
        preferred = " preferred" if member.get("archive_id") == data.get("preferred_archive_id") else ""
        print(f"  {member.get('archive_id')} ({member.get('source_machine')}, {member.get('sanitized_session_path')}){preferred}")
        print(f"    source environment: not represented in unified membership; source SHA256: {member.get('sanitized_sha256', '-')}")
    print("\nCONVERSATION")
    for event in data["conversation"]:
        print(f"\n[{event['timestamp'] or '-'}] {event['type']}")
        print(event["text"])
    if provenance:
        print("\nRELATIONS")
        for relation in data.get("relations", []):
            print(f"  {relation.get('relation_type')}: {relation.get('relation_id')}")


def stats(corpus: Corpus) -> dict[str, Any]:
    validation_counts = corpus.validation.get("counts", {})
    auxiliary = corpus.manifest.get("excluded_from_unified_prompt_views", {}).get("records", 0)
    provider_sessions = Counter(str(row.get("provider")) for row in corpus.logical)
    provider_prompts = Counter(
        str(corpus.logical_by_id[str(row["logical_session_id"])].get("provider"))
        for row in corpus.full_prompts
        if str(row["logical_session_id"]) in corpus.logical_by_id
    )
    months = Counter(str(row.get("timestamp", ""))[:7] for row in corpus.full_prompts if row.get("timestamp"))
    projects = Counter()
    for row in corpus.full_prompts:
        project = str(row.get("project") or row.get("cwd") or "-")
        projects[project] += 1
    return {
        "logical_full_sessions": len(corpus.logical),
        "archive_session_records": validation_counts.get("archive_sessions", len(corpus.membership)),
        "full_session_prompts": validation_counts.get("full_session_prompts", sum(row.get("member_count", 1) for row in corpus.full_prompts)),
        "reading_prompts": validation_counts.get("unified_full_session_prompt_rows", len(corpus.full_prompts)),
        "prompt_only_records": len(corpus.prompt_only),
        "auxiliary_events": auxiliary,
        "provider_sessions": dict(sorted(provider_sessions.items())),
        "provider_prompts": dict(sorted(provider_prompts.items())),
        "prompt_months": dict(sorted(months.items())),
        "top_projects_or_cwds": dict(projects.most_common(20)),
    }


def find_root(explicit: str | None) -> Path:
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    script_root = Path(__file__).resolve().parents[2]
    candidates.extend([Path.cwd() / "local/agent-history", script_root / "local/agent-history"])
    required = [FULL_PROMPT_FILE, LOGICAL_FILE, MEMBERSHIP_FILE, VALIDATION_FILE]
    for candidate in candidates:
        root = candidate.expanduser().resolve()
        if all((root / path).is_file() for path in required):
            return root
    locations = ", ".join(str(path.expanduser()) for path in candidates)
    raise QueryError(f"unified agent-history data not found; checked: {locations}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--text", help="case-insensitive substring search in prompts")
    action.add_argument("--list-sessions", action="store_true", help="list logical full sessions")
    action.add_argument("--session", metavar="LOGICAL_SESSION_ID", help="show one logical full session")
    action.add_argument("--stats", action="store_true", help="show frozen corpus statistics")
    parser.add_argument("--since", type=lambda value: date_bound(value, end=False), help="UTC date lower bound, YYYY-MM-DD")
    parser.add_argument("--until", type=lambda value: date_bound(value, end=True), help="UTC date upper bound, YYYY-MM-DD")
    parser.add_argument("--provider", help="claude or codex")
    parser.add_argument("--project", help="case-insensitive substring match against project/cwd")
    parser.add_argument("--class", dest="evidence_class", choices=("full_session", "prompt_only", "auxiliary"), default="full_session")
    parser.add_argument("--provenance", action="store_true", help="include archive and relation provenance with --session")
    parser.add_argument("--json", action="store_true", help="emit JSON for the selected query")
    parser.add_argument("--root", help="agent-history root, default: local/agent-history")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not any((args.text is not None, args.list_sessions, args.session, args.stats)):
        parser.error("choose one of --text, --list-sessions, --session, or --stats")
    if args.provenance and not args.session:
        parser.error("--provenance requires --session")
    if args.evidence_class == "auxiliary" and not args.stats:
        parser.error("auxiliary records are bookkeeping and have no conversational query view")
    if args.evidence_class == "prompt_only" and (args.list_sessions or args.session or args.provenance):
        parser.error("prompt_only records have no logical session or transcript view")
    if args.since and args.until and args.since >= args.until:
        parser.error("--since must be earlier than --until")
    try:
        corpus = Corpus(find_root(args.root))
        selected_provider = provider_filter(args.provider)
        if args.text is not None:
            if args.evidence_class == "prompt_only":
                rows = [
                    {**row, "provider": "claude-code"}
                    for row in corpus.prompt_only
                    if corpus.matches_common(
                        {**row, "provider": "claude-code"},
                        provider=selected_provider,
                        project=args.project,
                        since=args.since,
                        until=args.until,
                    )
                    and args.text.casefold() in str(row.get("prompt", "")).casefold()
                ]
                rows.sort(key=lambda row: (row.get("timestamp") or "", row.get("prompt_only_id", "")))
            else:
                rows = []
                for row in corpus.full_prompts:
                    logical = corpus.logical_by_id.get(str(row["logical_session_id"]))
                    if not logical:
                        continue
                    if selected_provider and logical.get("provider") != selected_provider:
                        continue
                    full_row = {**row, "provider": logical.get("provider")}
                    if not corpus.matches_common(full_row, provider=selected_provider, project=args.project, since=args.since, until=args.until):
                        continue
                    if args.text.casefold() in str(row.get("prompt", "")).casefold():
                        rows.append(full_row)
                rows.sort(key=lambda row: (row.get("timestamp") or "", row.get("logical_session_id", ""), row.get("unified_prompt_id", "")))
            if args.json:
                print(json.dumps(rows, ensure_ascii=False, indent=2, sort_keys=True))
            else:
                if not rows:
                    print("No matches.")
                elif args.evidence_class == "prompt_only":
                    for row in rows:
                        print(f"{row.get('prompt_only_id')}  {row.get('timestamp', '-')}")
                        print(f"  provider: {row.get('provider', 'claude-code')}  project/cwd: {row.get('project', '-') or '-'} / {row.get('cwd', '-') or '-'}")
                        print(f"  {snippet(str(row.get('prompt', '')), args.text)}")
                else:
                    print_text_results(rows, args.text)
            return 0

        if args.list_sessions:
            rows = list_session_rows(corpus, args)
            if args.json:
                print(json.dumps(rows, ensure_ascii=False, indent=2, sort_keys=True))
            else:
                for row in rows:
                    print(
                        f"{row['logical_session_id']}  {row.get('provider', '-')}  "
                        f"{row.get('start_time', '-') or '-'} .. {row.get('end_time', '-') or '-'}  "
                        f"prompts={row['prompt_count']} members={row['archive_member_count']}"
                    )
                    print(f"  native={row.get('native_session_id', '-')}  project/cwd={row.get('project', '-')} / {row.get('cwd', '-')}")
            return 0

        if args.session:
            row = corpus.logical_by_id.get(args.session)
            if row is None:
                raise QueryError(f"unknown logical_session_id: {args.session}")
            if not corpus.session_matches(row, provider=selected_provider, project=args.project, since=args.since, until=args.until):
                raise QueryError(f"logical session does not match the requested filters: {args.session}")
            data = render_session(corpus, row, args)
            if args.json:
                print(json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True))
            else:
                print_session(data, args.provenance)
            return 0

        result = stats(corpus)
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        else:
            print(f"logical full sessions: {result['logical_full_sessions']}")
            print(f"archive session records: {result['archive_session_records']}")
            print(f"full-session prompts: {result['full_session_prompts']}")
            print(f"reading/deduplicated prompts: {result['reading_prompts']}")
            print(f"prompt-only records: {result['prompt_only_records']}")
            print(f"auxiliary events: {result['auxiliary_events']}")
            print(f"provider sessions: {result['provider_sessions']}")
            print(f"provider prompts: {result['provider_prompts']}")
            print(f"prompt months: {result['prompt_months']}")
        return 0
    except QueryError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    raise SystemExit(main())
