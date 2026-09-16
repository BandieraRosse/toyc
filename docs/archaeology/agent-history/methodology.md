# 方法与格式约定

## 范围

Machine A 与 Machine B 的 source recovery 均已完成，acquisition 已按 pre-unify fingerprint `ab7742cd853842273462007d3c1fca638efe7f2f1dd604dc22cc4d7b76161802` 冻结。纳入边界由显式项目会话目录或经过审计的 Toyc cwd 决定；关键词只用于标签和检索，不用于删除会话。Conservative Unify V1 是冻结 machine 层之上的可重建派生视图，不扩大 acquisition 范围。

## 原始文件与镜像

原始 `.claude` 文件只读；本轮没有另行物化 Machine A raw snapshot，原始文件仍留在来源机器位置。镜像保持输入 JSONL 的事件顺序、每行一个 JSON 对象和所有非字符串结构。脱敏只递归替换字符串：用户目录、WSL home、Windows 私人路径、邮箱、电话号码、Bearer/API key/token/password/cookie/私钥模式分别替换为 `<USER_HOME>`、`<PRIVATE_PATH>`、`<REDACTED_PERSONAL_DATA>` 或 `<REDACTED_SECRET>`。公开仓库 URL、commit hash 和项目文件路径保留。脚本版本写入每条 session metadata；自动扫描命中只报告模式位置，不把命中内容写入 Git。

## Prompt ledger 与 session index

ledger 的 `source_pointer` 指向脱敏 session 的行号；`source_sha256` 是该镜像文件的 SHA256。自动标签是辅助字段，不能替代 prompt 原文，不确定时使用 `UNKNOWN`。session index 中缺失的 cwd、Git HEAD 或 commit 保持 `null`/空数组，不推测。

## Git 关联

`DIRECT` 只用于同一会话上下文同时出现明确 commit 操作和 hash 的情况；`POSSIBLE` 只表示 Git 相关上下文中出现 hash；没有证据则不生成链接，不能将时间接近写成事实。未来可用固定仓库对象复核并新增更高等级记录，但不覆盖原记录。

## 归因

会话镜像保留作者原话、agent 回复、工具调用、错误、重复和未完成路线。归因只能在后续 development episode 中按事件标记 `AUTHOR`、`AGENT`、`JOINT` 或 `UNKNOWN`，不统计代码/Token 百分比，也不以后来的 Git 结论改写当时文本。

## Machine B 与逻辑身份

Machine B 使用相同的 machine-independent archive 约束输出独立资料，不修改 Machine A 数据。Claude project 文件按 event-level `sessionId` 聚合；Codex 保持独立 provider namespace。archive 层身份仍是 `(source_machine, archive_id)`，不能用裸 session ID 代替物理来源。

Unified logical identity 使用 `(provider namespace, native session ID)`。当前 provider namespace 为 `claude-code` 与 `codex`；两者即使出现相同裸 ID 也不合并。一个 logical session 可以包含多个 archive member，例如 Machine A 的分片记录以及 Machine B 的 event-level normalized mirror。`preferred_archive_id` 只决定默认阅读成员，不改变证据身份和权威性。

## Conservative Unify V1

统一工具为 `tools/archaeology/unify_agent_history.py`，输出到各类数据目录下的 `unified/` 子目录。它遵守以下约束：

1. machine-specific sanitized session 与 index 始终是权威输入，不被重写或删除。
2. 每个 archive session 恰好映射到一个 logical full session；逻辑 ID 由身份字段确定性生成。
3. full-session prompt 只有在属于同一 logical session 且时间戳、正文完全相同时，才折叠为一条阅读记录；该记录保留全部 member pointer 与 SHA256。
4. `prompt_only` 不绑定 logical session；文本、cwd 或接近时间只能形成 candidate relation。
5. `file-history-snapshot` 是 auxiliary bookkeeping，不进入会话或 prompt 阅读视图。
6. relation 只表达已审计的 alias、exact duplicate 或 candidate，不删除 relation 两端的记录。

Unify V1 不执行 Git commit/session correlation、跨 native session 的语义合并、development episode 划分或 `AUTHOR`/`AGENT`/`JOINT` 归因。这些属于后续解释层，不能反向改变 archive 与 logical identity 层。
