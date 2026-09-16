# Machine A source summary

- machine_id: `machine-a`
- recovery status: preliminary source recovery complete
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

Machine A 初步候选 session ID 包括 `b80eb897-e9ac-446c-b07e-0694ab012d3d`、`629d30be-45e3-429e-8613-0c4aec269c25`、`d496480a-5dbc-47f8-bbca-72bfcddb8e3e`、`edc6e239-f98e-4056-a2ec-37c4f46955fe` 和 `ef2bd377-1fae-43ce-b258-cfedae75e5a0`。它们只是 selected-session 候选；必须等待 Machine B 恢复后检查前置会话、continuation、duplicate/copy 和更完整版本，当前不在 Git 中复制正文。
