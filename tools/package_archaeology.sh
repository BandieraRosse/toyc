#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output="$repo_dir/toyc-archaeology-bundle.md"

{
    printf '%s\n\n' '# Toyc 考古文档汇总'
    printf '%s\n\n' '本文件由 `tools/package_archaeology.sh` 根据 `docs/archaeology/` 下的 Markdown 文件生成。每个章节标题标注原始文件路径。'
    while IFS= read -r relative_path; do
        printf '%s\n\n' '---'
        printf '## 文件：`%s`\n\n' "$relative_path"
        cat "$repo_dir/$relative_path"
        printf '\n'
    done < <(cd "$repo_dir" && rg --files docs/archaeology -g '*.md' | sort)
} > "$output"

printf '已生成 %s\n' "$output"
