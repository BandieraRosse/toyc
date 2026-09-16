# Toyc Development Corpus

本目录只保存可进入 Git 的方法、工具说明、轻量 source summary 和未来精选历史文档。大规模数据位于仓库根目录 `local/agent-history/`；该目录故意被 Git 整体忽略，可以在两台机器之间整体复制，但不应提交。

Toyc Development Corpus 由多个 source machine 组成。Machine A 已恢复，Machine B 尚未恢复。任何 Machine A 统计、prompt timeline 或 attribution 都不能称为“完整 Toyc agent history”。完整统一语料只有在 Machine B 使用相同 schema 恢复后，经过 normalization → dedup → chronological merge 才能生成。

## Git 中保留的内容

- `README.md`、`methodology.md`：稳定入口与 schema/methodology。
- `sources/`：每台机器的轻量摘要，不保存真实用户目录或全量文件表。
- `../../../tools/archaeology/`：archive 与 verify 工具。

ignored 本地结构固定为 `raw/<machine>/`、`sanitized/<machine>/`、`prompts/<machine>/`、`indexes/<machine>/`、`manifests/<machine>/`、`reports/<machine>/` 和 `tmp/`。其中 machine-a 与 machine-b 永不互相覆盖；只有两台机器均完成后才允许写入各类 `unified/`。完整 session、全量 prompt、索引、manifest、报告和中间产物均属于 ignored corpus。

生成与验证示例：

```sh
python3 tools/archaeology/archive_agent_history.py \\
  --machine machine-a \\
  --prompt-index <CLAUDE_DATA>/history.jsonl \\
  --source-root toyc=<CLAUDE_DATA>/projects/tcc \\
  --source-root tinylibc=<CLAUDE_DATA>/projects/Tinylibc
python3 tools/archaeology/verify_agent_history.py --machine machine-a
```

Machine B 直接改用 `--machine machine-b` 和该机器自己的 source roots；source path 与 output root 相互独立，不要求相同用户名、home 或项目绝对路径。合并时按 `(source_machine, session_id)` 保持来源身份，并保留原始 SHA256。当前不生成假的 unified 数据，也不把 307 个 Machine A session 复制为 tracked selected sessions；候选 ID 仅记录在 source summary，须等 Machine B 恢复后复核。
