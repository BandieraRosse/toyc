#!/usr/bin/env bash
set -eu

# Generate Rasterfall working-tree snapshots for AI-assisted work.
# Only text source and current documentation are copied; assets stay in the index.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
OUTPUT_DIR=tmp
PROJECT_OUTPUT="$OUTPUT_DIR/rasterfall-project.txt"
SOURCE_OUTPUT="$OUTPUT_DIR/rasterfall-source.txt"
DOC_OUTPUT="$OUTPUT_DIR/rasterfall-docs.txt"

mkdir -p "$OUTPUT_DIR"

SOURCE_LIST=$(mktemp "$OUTPUT_DIR/rasterfall-source-list.XXXXXX")
DOC_LIST=$(mktemp "$OUTPUT_DIR/rasterfall-doc-list.XXXXXX")
SOURCE_STATS=$(mktemp "$OUTPUT_DIR/rasterfall-source-stats.XXXXXX")
cleanup() {
    rm -f "$SOURCE_LIST" "$DOC_LIST" "$SOURCE_STATS"
}
trap cleanup EXIT HUP INT TERM

# Null-delimited lists keep file collection safe for spaces in paths. Sorting
# with the C locale makes the order independent of the machine's locale.
{
    find "$ROOT/rasterfall/src" "$ROOT/rasterfall/include" \
        "$ROOT/gpu/src" "$ROOT/gpu/include" "$ROOT/gpu/shaders" \
        "$ROOT/windows/src" "$ROOT/windows/include" \
        "$ROOT/lib" "$ROOT/include" -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.inc' -o -name '*.vert' \
           -o -name '*.frag' -o -name '*.comp' \) -print0
    find "$ROOT/tools" -maxdepth 1 -type f \
        \( -name 'gpu_*.ps1' -o -name 'gpu_*.py' -o -name 'rf_*.py' \) -print0
    printf '%s\0' "$ROOT/Makefile" "$ROOT/windows/Makefile" \
        "$ROOT/windows/NativeCodex.ps1" "$ROOT/scripts/merge-rasterfall.sh"
} | LC_ALL=C sort -zu >"$SOURCE_LIST"
{
    find "$ROOT/docs/rasterfall" -path "$ROOT/docs/rasterfall/archive" -prune -o \
        -type f -name '*.md' -print0
    printf '%s\0' "$ROOT/AGENTS.md" "$ROOT/docs/README.md" \
        "$ROOT/docs/repository/documentation.md" "$ROOT/rasterfall/README.md"
} | LC_ALL=C sort -zu >"$DOC_LIST"

source_count=0
c_count=0
h_count=0
total_lines=0
while IFS= read -r -d '' file; do
    lines=$(wc -l <"$file")
    relative=${file#"$ROOT/"}
    printf '%s\t%s\n' "$lines" "$relative" >>"$SOURCE_STATS"
    source_count=$((source_count + 1))
    case "$file" in
        *.c) c_count=$((c_count + 1)) ;;
        *.h) h_count=$((h_count + 1)) ;;
    esac
    total_lines=$((total_lines + lines))
done <"$SOURCE_LIST"

write_snapshot() {
    output=$1
    title=$2
    file_list=$3
    show_lines=$4
    temporary=$(mktemp "$output.tmp.XXXXXX")

    printf '%s\n' "$title" >"$temporary"
    printf 'Generated from: %s\n\n' "$ROOT" >>"$temporary"

    while IFS= read -r -d '' file; do
        relative=${file#"$ROOT/"}
        if [ "$show_lines" -eq 1 ]; then
            lines=$(wc -l <"$file")
            printf '===== BEGIN FILE: %s (lines: %s) =====\n' \
                "$relative" "$lines" >>"$temporary"
        else
            printf '===== BEGIN FILE: %s =====\n' "$relative" >>"$temporary"
        fi
        cat "$file" >>"$temporary"
        printf '\n===== END FILE: %s =====\n\n' "$relative" >>"$temporary"
    done <"$file_list"

    mv -f "$temporary" "$output"
}

write_project_index() {
    temporary=$(mktemp "$PROJECT_OUTPUT.tmp.XXXXXX")
    if git_hash=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null); then
        :
    else
        git_hash='unavailable'
    fi
    generated_at=$(date '+%Y-%m-%d %H:%M:%S %z')

    printf '%s\n\n' 'Rasterfall project index' >"$temporary"
    printf 'Git commit: %s\n' "$git_hash" >>"$temporary"
    printf 'Generated at: %s\n\n' "$generated_at" >>"$temporary"

    printf '%s\n' 'Rasterfall source and asset tree:' >>"$temporary"
    (cd "$ROOT" && find rasterfall gpu windows -path '*/private-assets' -prune -o \
        -path '*/build' -prune -o -print | LC_ALL=C sort) >>"$temporary"
    printf '\nC file count: %s\n' "$c_count" >>"$temporary"
    printf 'H file count: %s\n' "$h_count" >>"$temporary"
    printf 'Source and build file count: %s\n' "$source_count" >>"$temporary"
    printf 'Total source and build lines: %s\n\n' "$total_lines" >>"$temporary"

    printf '%s\n' 'Largest source files (lines, path):' >>"$temporary"
    LC_ALL=C sort -nr -k1,1 -k2,2 "$SOURCE_STATS" | head -n 10 >>"$temporary"
    printf '\n%s\n' 'Current documentation files:' >>"$temporary"
    while IFS= read -r -d '' file; do
        printf '%s\n' "${file#"$ROOT/"}" >>"$temporary"
    done <"$DOC_LIST"

    mv -f "$temporary" "$PROJECT_OUTPUT"
}

write_project_index
write_snapshot "$SOURCE_OUTPUT" 'Rasterfall source and header files' "$SOURCE_LIST" 1
write_snapshot "$DOC_OUTPUT" 'Rasterfall documentation' "$DOC_LIST" 0

rm -f "$OUTPUT_DIR/rasterfall-source-and-headers.txt"
printf 'Wrote %s\nWrote %s\nWrote %s\n' \
    "$PROJECT_OUTPUT" "$SOURCE_OUTPUT" "$DOC_OUTPUT"
