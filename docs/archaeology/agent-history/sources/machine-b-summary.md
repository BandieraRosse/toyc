# Machine B source summary

- machine_id: `machine-b`
- recovery status: complete after Claude event-level session-boundary normalization and unbound-event audit; incorporated into Conservative Unify V1
- previous provisional model: one Claude project JSONL file = one session
- correction: previous Machine B session counts invalidated because event files can contain multiple sessionId values
- Claude project sources: `308` JSONL files, `206` logical sessions, `57` cross-file sessions, `2,278` retained unbound events
- session sources: `560` (`206` Claude Code, `354` Codex)
- full-session prompts: `2,021` (`1,525` Claude, `496` Codex)
- prompt-only global-history evidence: `1,492` (`1,453` WSL, `39` Windows)
- time_range: `2026-07-31 .. 2026-09-16`
- raw_bytes: Claude/Codex source manifests retained; raw snapshots not materialized
- sanitized_bytes: corrected Claude mirrors plus retained Codex mirrors under ignored corpus
- sanitization_leak_hits: `0`
- original secret-pattern replacements: `63` in the retained Codex/earlier source audit; corrected Claude scan has no residual leak
- archive_schema_version: `machine-independent-v1`
- raw snapshot: not materialized; source manifests contain neutral source references and SHA256 values
- prompt-only history: global records are explicitly `claude_global_history_prompt` with `evidence_scope=prompt_only`; no complete session was fabricated

Machine B restores complete Toyc-cwd sessions from the local WSL Claude project directories and Codex
rollout directory. Claude events are grouped by their event-level `sessionId`; exact duplicate
events across files are conservatively removed and unbound events are reported separately. Two
Claude plan markdown files are archived as auxiliary sources. The WSL and Windows global Claude
histories are retained as prompt-only evidence and cannot establish assistant replies, tool calls,
or complete session context.

The unbound pool was audited event by event. All 2,278 records are Claude `file-history-snapshot`
bookkeeping. None is a user prompt, assistant answer, tool call, tool result, progress event, or
summary; no direct/strong identity path to a logical session was found, so all remain unbound. The
normalized event report is in `reports/machine-b/unbound-events.jsonl`.

The complete Machine B corpus is ignored under `local/agent-history/`; Machine A remains unchanged.
The final pre-unify relation report records 185 `same_native_session` aliases, 268
`exact_source_duplicate` relations, and no probable/weak duplicate relations. Conservative Unify V1
has generated identity, membership, relation and prompt-reading views without semantic merge or
deletion. Prompt-only matches remain candidates only; development episodes and attribution have not
been generated.
