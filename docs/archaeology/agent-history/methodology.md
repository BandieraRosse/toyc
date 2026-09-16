# 方法与格式约定

## 范围

当前已完成的是 Machine A 的 Claude Code preliminary source recovery。纳入边界由显式项目会话目录决定：当前 tcc/ToyCCompiler 项目目录和早期 Tinylibc 项目目录。关键词只用于标签和检索，不用于删除会话。Machine B 完成前不能据此宣称覆盖完整 Toyc agent history。

## 原始文件与镜像

原始 `.claude` 文件只读；本轮没有另行物化 Machine A raw snapshot，原始文件仍留在来源机器位置。镜像保持输入 JSONL 的事件顺序、每行一个 JSON 对象和所有非字符串结构。脱敏只递归替换字符串：用户目录、WSL home、Windows 私人路径、邮箱、电话号码、Bearer/API key/token/password/cookie/私钥模式分别替换为 `<USER_HOME>`、`<PRIVATE_PATH>`、`<REDACTED_PERSONAL_DATA>` 或 `<REDACTED_SECRET>`。公开仓库 URL、commit hash 和项目文件路径保留。脚本版本写入每条 session metadata；自动扫描命中只报告模式位置，不把命中内容写入 Git。

## Prompt ledger 与 session index

ledger 的 `source_pointer` 指向脱敏 session 的行号；`source_sha256` 是该镜像文件的 SHA256。自动标签是辅助字段，不能替代 prompt 原文，不确定时使用 `UNKNOWN`。session index 中缺失的 cwd、Git HEAD 或 commit 保持 `null`/空数组，不推测。

## Git 关联

`DIRECT` 只用于同一会话上下文同时出现明确 commit 操作和 hash 的情况；`POSSIBLE` 只表示 Git 相关上下文中出现 hash；没有证据则不生成链接，不能将时间接近写成事实。未来可用固定仓库对象复核并新增更高等级记录，但不覆盖原记录。

## 归因

会话镜像保留作者原话、agent 回复、工具调用、错误、重复和未完成路线。归因只能在后续 development episode 中按事件标记 `AUTHOR`、`AGENT`、`JOINT` 或 `UNKNOWN`，不统计代码/Token 百分比，也不以后来的 Git 结论改写当时文本。

## Machine B 合并

Machine B 使用相同 CLI 和 `machine-independent-v1` schema 输出独立资料，不修改 Machine A 数据。合并键是 `(source_machine, session_id)`；prompt ledger 使用 `(source_machine, session_id, turn_index)`。两台机器均完成后才能执行 normalization、跨机器 duplicate/copy 判断和 chronological merge，并生成 `unified/`。总览必须明确 Machine A、Machine B 和合并后的覆盖范围，不能把任一机器的 archive summary 称为完整历史。
