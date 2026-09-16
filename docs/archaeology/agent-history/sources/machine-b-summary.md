# Machine B source summary

- machine_id: `machine-b`
- recovery status: complete after Claude event-level session-boundary normalization and unbound-event audit
- previous provisional model: one Claude project JSONL file = one session
- correction: previous Machine B session counts invalidated because event files can contain multiple sessionId values
- Claude project sources: `21` JSONL files, `16` logical sessions, `2` cross-file sessions, `183` retained unbound events
- session sources: `370` (`16` Claude Code, `354` Codex)
- full-session prompts: `643` (`147` Claude, `496` Codex)
- prompt-only global-history evidence: `260` (`236` WSL, `24` Windows)
- time_range: `2026-07-31 .. 2026-09-16`
- raw_bytes: Claude/Codex source manifests retained; raw snapshots not materialized
- sanitized_bytes: corrected Claude mirrors plus retained Codex mirrors under ignored corpus
- sanitization_leak_hits: `0`
- original secret-pattern replacements: `63` in the retained Codex/earlier source audit; corrected Claude scan has no residual leak
- archive_schema_version: `machine-independent-v1`
- raw snapshot: not materialized; source manifests contain neutral source references and SHA256 values
- prompt-only history: global records are explicitly `claude_global_history_prompt` with `evidence_scope=prompt_only`; no complete session was fabricated

Machine B restores complete Toyc-cwd sessions from the local Claude project directory and Codex
rollout directory. Claude events are grouped by their event-level `sessionId`; exact duplicate
events across files are conservatively removed and unbound events are reported separately. Two
Claude plan markdown files are archived as auxiliary sources. The WSL and Windows global Claude
histories are retained as prompt-only evidence and cannot establish assistant replies, tool calls,
or complete session context.

The unbound pool was audited event by event. All 183 records are Claude file-history bookkeeping:
92 snapshots (LOW) and 91 deltas (MEDIUM). None is a user prompt, assistant answer, tool call,
tool result, progress event, or summary; no direct/strong identity path to a logical session was
found, so all remain unbound. The audit is in `reports/machine-b/unbound-event-audit.jsonl`.

The complete Machine B corpus is ignored under `local/agent-history/`; Machine A remains unchanged.
Cross-machine output is candidate-only: 0 exact full-session duplicates, 0 same-session/version
candidates, 71 POSSIBLE prompt-only → full-session candidates, 0 Machine A prompt candidates, and
25 generic-text collisions explicitly excluded from duplicate counts. No semantic merge, deletion,
development episode, or unified corpus was generated.
