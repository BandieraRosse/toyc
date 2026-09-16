# Agent History 搜集与预处理记录

本文记录本轮 Toyc Agent History 的实际搜集、边界修正、预处理和封账过程。它是方法与证据链记录，不是 unified archive；`local/agent-history/` 仍是被 Git 忽略的本地数据区。

## 目标与边界

目标是保存 Machine A / Machine B 的 Claude Code 与 Codex 开发会话，并使每条记录可以追溯到物理 source、native session ID、脱敏镜像和 SHA256。搜集范围限定为 Toyc/Tinylibc/Rasterfall 主线相关的本地 agent history；没有扩展到其它项目，也没有把 Git 提交与会话强行关联。

物理机器身份与项目身份分离：Machine B 同时包含 Codex rollout、WSL Claude project 和 WSL `/mnt/d/Tinylibc` project。路径变化不自动产生新的 machine，也不自动证明是新的 logical session。

## 搜集阶段

### Machine A

Machine A 使用 `tools/archaeology/archive_agent_history.py` 从 Claude project source 和 global history prompt index 生成独立的 sanitized session、prompt ledger、index、manifest 和 archive report。共恢复 307 个 indexed source/session records，原始 source manifest 保留 raw 与 sanitized SHA256；raw snapshot 没有复制进仓库。

### Machine B 初始恢复

Machine B 的 Codex rollout 按项目 cwd 筛选，只纳入 Toyc cwd 的 rollout，得到 354 个 indexed records。早期 Claude 恢复曾暂时采用“一个 project JSONL 文件等于一个 session”的模型；该模型后来被废止。

### Claude event-level 修正

Claude project JSONL 文件可能包含多个 `sessionId`，也可能跨文件保存同一 session。因此使用 `tools/archaeology/repair_machine_b_claude.py` 按 event-level `sessionId` 分组，跨文件合并同一 native session，并对跨文件的精确重复 event 保守去重；无法绑定 session 的事件单独保留并审计。

修正后的 WSL `Desktop/tcc` 数据为 268 个 source JSONL、185 个 logical Claude session。随后发现 WSL `/mnt/d/Tinylibc` 是未归档的主线 namespace，单独在临时目录中处理后得到 40 个 source、21 个 logical session，并以增量方式加入 Machine B；没有覆盖既有 A/B sanitized source。

## 预处理规则

1. source 文件按逐行 JSONL 读取；非法 JSON、空 source 和边界异常记录进入审计，不静默丢弃。
2. 字符串递归脱敏，路径替换为 neutral label，敏感模式先记录命中位置，再生成 sanitized mirror；原始 `.claude` 文件不复制进仓库。
3. raw source 和 sanitized session 分别保存 SHA256。prompt ledger 保存其 session archive ID、sanitized source pointer、行号和 source SHA256。
4. native session identity 由 provider/source domain 与 native session ID 共同解释；不同机器上的同一 Claude native ID 可以形成 alias，Claude 与 Codex 不共享裸 ID namespace。
5. `full_session`、`prompt_only`、`file-history-snapshot` 是不同 evidence class。global `history.jsonl` 只作为 `prompt_only`；无 session ID 的 file-history bookkeeping 不提升为 prompt。
6. isolated prompt text、相同 cwd、接近时间和 generic prompt 只能产生 candidate relation，不能证明 session identity。

## 发现与修正过的边界

- 初始 Machine B Claude 统计被“文件数等于 session 数”的假设污染，之后改为 event-level session boundary；旧统计不再使用。
- WSL `Desktop/tcc` Claude source 与 Machine A 存在 185 个 native-session alias 和 268 个 raw-source exact duplicate；它们保留为 relation，不重复计入 unique logical session。
- WSL `/mnt/d/Tinylibc` 的 21 个 session 与 A、B 既有 Claude/Codex 在 native ID、raw SHA、sanitized SHA 和 normalized complete-message fingerprint 上均无 overlap，因此计为 21 个新增 logical session。
- 1,893 个原有 unbound event 与新增 385 个 event 均确认是 `file-history-snapshot` metadata；它们进入 auxiliary evidence，不进入 prompt ledger。
- Machine A 存在 8 个 user-only source session；interaction completeness 只作为未来 metadata，不把 full source 降级成 prompt-only。

## 审计与封账

Pre-Unify 审计检查了 source existence、session/prompt pointer、JSONL 合法性、sanitized SHA256、native ID 覆盖、跨机器 exact overlap、时间格式和 evidence class。最终 archive-layer source entries 为 969，unique raw source identities 为 701，indexed session records 为 867，full-session prompts 为 3,394，prompt-only records 为 1,492，file-history-snapshot events 为 2,278。

logical full session 不按 indexed record 直接求和，而按 native identity 和 exact relation 计算：Machine A 208 个 native ID，加 Machine B Codex 320 个 native ID，加新增 Tinylibc Claude 21 个；既有 185 个 Machine B WSL Claude 是 Machine A alias，因此最终为 549 个 unique logical full sessions。

封账报告位于 `local/agent-history/reports/pre-unify-audit.md`、`pre-unify-duplicate-candidates.jsonl` 和 `pre-unify-manifest.json`。旧的 pre-WSL duplicate report 已标为 deprecated。最终 pre-unify fingerprint 为：

```text
ab7742cd853842273462007d3c1fca638efe7f2f1dd604dc22cc4d7b76161802
```

状态为：

```text
ACQUISITION FROZEN
READY FOR CONSERVATIVE UNIFY
```

## Conservative Unify V1

预处理封账后运行 `tools/archaeology/unify_agent_history.py`，在不修改 machine 层的前提下生成各类 `unified/` 派生视图。867 个 indexed session record 全部映射到 549 个 logical full session；3,394 条 full-session prompt 在同一 logical session 内按完全相同的时间戳与正文折叠为 2,179 条阅读记录，每条仍保存全部物理 pointer。1,492 条 prompt-only record 保持未绑定，2,278 条 file-history-snapshot 不进入 conversational view。

统一层继续保留 185 个 `same_native_session`、268 个 `exact_source_duplicate` 和 1,912 个 `prompt_only_candidate` relation。duplicate 两端没有删除，原始 archive session 没有 canonicalize；`preferred_archive_id` 只是显示选择。验证报告位于 `local/agent-history/reports/unified/validation.json`，manifest 位于 `local/agent-history/manifests/unified/manifest.json`。

Unify V1 仍未建立 Git commit/session correlation，也没有执行语义 session merge、development episode 或贡献归因。这些任务必须以 unified identity 和 relation 为输入另行开展，不能反向改写本层记录。
