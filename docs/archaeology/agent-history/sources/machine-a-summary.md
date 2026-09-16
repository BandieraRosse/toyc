# Machine A source summary

- machine_id: `machine-a`
- recovery status: source recovery complete; incorporated into Conservative Unify V1
- sessions: `307`
- prompts: `1373`
- time_range: `2026-07-02 .. 2026-07-27`
- raw_bytes: `287016077`
- sanitized_bytes: `294086522`
- sanitization_leak_hits: `0`
- archive_schema_version: `machine-independent-v1`
- local_archive_path: `local/agent-history/`
- raw snapshot: not yet materialized; source files remain at original machine location

完整逐文件 SHA256 manifest、session index、prompt ledger 和 archive report 只保存在 ignored local corpus。这里的 leak 数字是迁移后 corpus residual scan；原始输入中被脱敏器识别并替换的模式不等同于残留泄漏。

Machine A 的 307 个 archive record 对应 208 个 Claude native session ID；同一 native session 的分片记录在 unified 层共享 logical session，但各 archive member、pointer 与 SHA256 均保留。Machine A 与 Machine B 的 alias、duplicate 和更完整镜像已经在最终 pre-unify audit 与 Conservative Unify V1 中处理；selected-session 筛选仍属于后续解释工作，当前不在 Git 中复制正文。
