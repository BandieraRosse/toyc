#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output="$repo_dir/tmp/toyc-archaeology-bundle.md"
mkdir -p "$repo_dir/tmp"
tmp_output=$(mktemp "$output.tmp.XXXXXX")
trap 'rm -f "$tmp_output"' EXIT
repository_commit=$(git -C "$repo_dir" rev-parse HEAD)
generated_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)
checksum_placeholder=0000000000000000000000000000000000000000000000000000000000000000

{
    printf '%s\n\n' '# Toyc 考古文档汇总'
    printf '%s\n' 'Archaeology snapshot: Toyc archaeology documents'
    printf 'Repository commit: %s\n' "$repository_commit"
    printf 'Generated at: %s\n' "$generated_at"
    printf '%s\n' 'Source directory: docs/archaeology/'
    printf 'Bundle SHA256: %s\n\n' "$checksum_placeholder"
    printf '%s\n\n' '本文件由 `tools/package_archaeology.sh` 根据 `docs/archaeology/` 下的文档生成。每个章节标题标注原始文件路径，文件按路径稳定排序。'
    printf '%s\n\n' '校验说明：Bundle SHA256 是将本行的 64 位十六进制值替换为 64 个零后，对整个文件计算得到的 SHA256。'
    while IFS= read -r relative_path; do
        printf '%s\n\n' '---'
        printf '## 文件：`%s`\n\n' "$relative_path"
        cat "$repo_dir/$relative_path"
        printf '\n'
    done < <(
        cd "$repo_dir"
        find docs/archaeology -type f \
            \( -name '*.md' -o -name '*.markdown' -o -name '*.txt' -o -name '*.rst' \) \
            -print | LC_ALL=C sort
    )
} > "$tmp_output"

bundle_sha256=$(sha256sum "$tmp_output" | awk '{print $1}')
sed -i "s/^Bundle SHA256: $checksum_placeholder$/Bundle SHA256: $bundle_sha256/" "$tmp_output"

mv "$tmp_output" "$output"
trap - EXIT

printf '已生成 %s\n' "$output"
