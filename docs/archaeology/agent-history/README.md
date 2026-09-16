# Toyc Development Corpus

本目录只保存可进入 Git 的方法、工具说明、轻量 source summary 和未来精选历史文档。大规模数据位于仓库根目录 `local/agent-history/`；该目录故意被 Git 整体忽略，可以在两台机器之间整体复制，但不应提交。搜集与预处理的实际过程见 [collection-and-preprocessing.md](collection-and-preprocessing.md)。

Toyc Development Corpus 由多个 source machine 组成。Machine A 与 Machine B 均已恢复，acquisition 已冻结。Conservative Unify V1 已生成：它只增加 logical session、archive membership、relation 和分层 prompt 阅读视图，不删除、改写或 canonicalize 任一 machine 层记录。

## Git 中保留的内容

- `README.md`、`methodology.md`：稳定入口与 schema/methodology。
- `sources/`：每台机器的轻量摘要，不保存真实用户目录或全量文件表。
- `../../../tools/archaeology/`：archive、boundary repair、verify 与 conservative unify 工具。

ignored 本地结构固定为 `raw/<machine>/`、`sanitized/<machine>/`、`prompts/<machine>/`、`indexes/<machine>/`、`manifests/<machine>/`、`reports/<machine>/` 和 `tmp/`。其中 machine-a 与 machine-b 永不互相覆盖；派生视图分别写入 `prompts/unified/`、`indexes/unified/`、`manifests/unified/` 和 `reports/unified/`。完整 session、全量 prompt、索引、manifest、报告和中间产物均属于 ignored corpus。

生成与验证示例：

```sh
python3 tools/archaeology/archive_agent_history.py \\
  --machine machine-a \\
  --prompt-index <CLAUDE_DATA>/history.jsonl \\
  --source-root toyc=<CLAUDE_DATA>/projects/tcc \\
  --source-root tinylibc=<CLAUDE_DATA>/projects/Tinylibc
python3 tools/archaeology/verify_agent_history.py --machine machine-a
python3 tools/archaeology/unify_agent_history.py
```

Machine B 的 Claude 与 Codex source 形态不同，使用同目录下的 recovery 与 boundary repair 工具；Claude project boundary 按 event-level `sessionId` 处理，不能把文件数当作 session 数。source path 与 output root 相互独立，不要求相同用户名、home 或项目绝对路径。

Unify V1 以 `(provider namespace, native session ID)` 建立确定性的 logical session ID。867 个 archive session record 映射为 549 个 logical full session；3,394 条 full-session prompt 在同一 logical session 内只按“时间戳与正文均完全相同”折叠为 2,179 条阅读记录，所有物理 pointer 仍保留。1,492 条 `prompt_only` 和 2,278 条 `file-history-snapshot` 始终与完整会话分层。统一报告与验证结果见 `local/agent-history/reports/unified/`。

## Querying the unified history

查询工具只读取 unified 派生数据和已脱敏的 session mirror，不修改 archive、unified 数据或 logical identity：

```sh
python3 tools/archaeology/query_agent_history.py --stats
python3 tools/archaeology/query_agent_history.py --text "自举"
python3 tools/archaeology/query_agent_history.py --text "测试" --since 2026-06-29 --until 2026-07-04 --provider claude
python3 tools/archaeology/query_agent_history.py --list-sessions --project Tinylibc
python3 tools/archaeology/query_agent_history.py --session <logical-session-id>
python3 tools/archaeology/query_agent_history.py --session <logical-session-id> --provenance
```

默认查询 `full_session`；需要查询未绑定的 global history 时显式使用 `--class prompt_only`。`auxiliary` 是
file-history bookkeeping，不作为普通会话或 prompt 搜索结果。支持 `--json` 输出机器可读结果。
