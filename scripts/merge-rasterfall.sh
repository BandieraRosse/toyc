#!/usr/bin/env bash
set -eu

# Generate Rasterfall working-tree snapshots for AI-assisted work.
# Only text source and current documentation are copied; assets stay in the index.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
OUTPUT_DIR=tmp
PROJECT_OUTPUT="$OUTPUT_DIR/rasterfall-project.txt"
DOC_OUTPUT="$OUTPUT_DIR/rasterfall-docs.txt"

mkdir -p "$OUTPUT_DIR"

WORK_DIR=$(mktemp -d "$OUTPUT_DIR/rasterfall-merge.XXXXXX")
SOURCE_LIST="$WORK_DIR/source-list"
DOC_LIST="$WORK_DIR/doc-list"
SOURCE_STATS="$WORK_DIR/source-stats"
GROUP_STATS="$WORK_DIR/group-stats"
GENERATED_STATS="$WORK_DIR/generated-stats"
SOURCE_GROUPS='runtime gameplay maps rendering characters platform diagnostics'
cleanup() {
    rm -f -- "$WORK_DIR"/*
    rmdir -- "$WORK_DIR"
}
trap cleanup EXIT HUP INT TERM

# Current-layout routing, not a compatibility layer for older repository versions.
# Each selected file has one owner. Matching basenames keeps C/header pairs together;
# directory and specific adapter rules take precedence over general family names.
source_group() {
    relative=$1
    name=${relative##*/}
    case "$relative" in
        gpu/src/rf_gpu_graphics_spirv.inc) printf '%s' generated; return ;;
        rasterfall/src/dev-tests/*) printf '%s' diagnostics; return ;;
    esac
    case "$name" in
        *_test.*|*_test.inc|*_tests.*|rasterfall_logic_test.inc)
            printf '%s' diagnostics; return ;;
    esac
    case "$relative" in
        tools/map_*|tools/world_layout_export.py) printf '%s' maps; return ;;
        tools/generate_gpu_*) printf '%s' platform; return ;;
        tools/*) printf '%s' diagnostics; return ;;
        include/rasterfall_public_protocol.h) printf '%s' gameplay; return ;;
        gpu/*|windows/*|lib/*|include/*|Makefile|scripts/*)
            printf '%s' platform; return ;;
        rasterfall/lib/game*|rasterfall/lib/sfx.c) printf '%s' gameplay; return ;;
        rasterfall/lib/*) printf '%s' maps; return ;;
    esac
    case "$name" in
        toy_map.h|rasterfall_map*|rasterfall_world_content*) printf '%s' maps ;;
        toy_game*|toy_mesh_weaver.h|rf_weaver_blueprints_generated.h|rasterfall_session*|rasterfall_net*|rasterfall_units*|rasterfall_roster*|rasterfall_ai*|rasterfall_action*|rf_rts.c|rf_rts.h)
            printf '%s' gameplay ;;
        rasterfall_character*|rasterfall_model*|rasterfall_animation*|rasterfall_actor_animation*|rasterfall_motion_presentation*|rasterfall_humanoid*|rasterfall_enemy*|rasterfall_block_character*|rasterfall_glb*|rasterfall_vmd*|rasterfall_rifle_pose*|rasterfall_calibration*|rasterfall_viewmodel*|rf_viewmodel_contract*|rf_gpu_scene_pose*|rf_gpu_scene_enemy*|rf_gpu_scene_actor*|rf_outpost_actor_showcase*|rf_outpost_rifle_cycle*|rf_outpost_weapon_cycle*)
            printf '%s' characters ;;
        rasterfall_render*|rasterfall_draw*|rasterfall_prop*|rasterfall_sky*|rasterfall_world_light*|rasterfall_effect*|rasterfall_colors*|rf_gpu*|rf_scene_id_set*|rf_mesh_weaver_presentation*|rf_mesh_weaver_layout_generated.h)
            printf '%s' rendering ;;
        *)
            case "$relative" in
                rasterfall/src/render/*) printf '%s' rendering ;;
                *) printf '%s' runtime ;;
            esac ;;
    esac
}

group_title() {
    case "$1" in
        runtime) printf '%s' 'Runtime, Core Host, input, UI, commands and story' ;;
        gameplay) printf '%s' 'Gameplay, session, combat, navigation, RTS and networking' ;;
        maps) printf '%s' 'Map parsing, runtime maps, world content and layout tools' ;;
        rendering) printf '%s' 'World rendering, GPU Scene, lighting and effects' ;;
        characters) printf '%s' 'Characters, models, animation, poses and viewmodel' ;;
        platform) printf '%s' 'Platform, GPU backend, shaders, shared libraries and build' ;;
        diagnostics) printf '%s' 'Tests, captures, diagnostics and lab tools' ;;
    esac
}

: >"$SOURCE_STATS"
: >"$GROUP_STATS"
: >"$GENERATED_STATS"
for group in $SOURCE_GROUPS; do
    : >"$WORK_DIR/$group-list"
done

# Null-delimited lists keep file collection safe for spaces in paths. Sorting
# with the C locale makes the order independent of the machine's locale.
{
    find "$ROOT/rasterfall/src" "$ROOT/rasterfall/include" "$ROOT/rasterfall/lib" \
        "$ROOT/gpu/src" "$ROOT/gpu/include" "$ROOT/gpu/shaders" \
        "$ROOT/windows/src" "$ROOT/windows/include" \
        "$ROOT/lib" "$ROOT/include" -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.inc' -o -name '*.vert' \
           -o -name '*.frag' -o -name '*.comp' -o -name '*.glsl' \) -print0
    find "$ROOT/tools" -maxdepth 1 -type f \
        \( -name 'gpu_*.ps1' -o -name 'gpu_*.py' -o -name 'rf_*.py' \
           -o -name 'rf_*test.c' -o -name 'rasterfall_*test.c' \
           -o -name 'map_*.py' -o -name 'world_layout_export.py' \
           -o -name 'combat_lab.ps1' -o -name 'rts_command_check.py' \
           -o -name 'generate_gpu_*.py' \) -print0
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
    relative=${file#"$ROOT/"}
    group=$(source_group "$relative")
    if [ "$group" = generated ]; then
        read -r lines bytes <<<"$(wc -lc <"$file")"
        printf '%s\t%s\t%s\n' "$bytes" "$lines" "$relative" >>"$GENERATED_STATS"
        continue
    fi
    # Count and concatenate the same per-file copy, even while another session edits.
    captured="$WORK_DIR/source-$source_count"
    cp -- "$file" "$captured"
    read -r lines bytes <<<"$(wc -lc <"$captured")"
    printf '%s\0%s\0%s\0' "$file" "$captured" "$lines" >>"$WORK_DIR/$group-list"
    printf '%s\t%s\t%s\t%s\n' "$lines" "$relative" "$group" "$bytes" >>"$SOURCE_STATS"
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
    temporary=$(mktemp "$WORK_DIR/snapshot.XXXXXX")

    printf '%s\n' "$title" >"$temporary"
    printf 'Generated from: %s\n' "$ROOT" >>"$temporary"
    printf 'Routing and companion collections: rasterfall-project.txt\n\n' >>"$temporary"

    while IFS= read -r -d '' file; do
        relative=${file#"$ROOT/"}
        if [ "$show_lines" -eq 1 ]; then
            IFS= read -r -d '' captured
            IFS= read -r -d '' lines
            printf '===== BEGIN FILE: %s (lines: %s) =====\n' \
                "$relative" "$lines" >>"$temporary"
        else
            captured=$file
            printf '===== BEGIN FILE: %s =====\n' "$relative" >>"$temporary"
        fi
        cat "$captured" >>"$temporary"
        printf '\n===== END FILE: %s =====\n\n' "$relative" >>"$temporary"
    done <"$file_list"

    mv -f "$temporary" "$output"
}

write_project_index() {
    temporary=$(mktemp "$WORK_DIR/index.XXXXXX")
    if git_hash=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null); then
        :
    else
        git_hash='unavailable'
    fi
    generated_at=$(date '+%Y-%m-%d %H:%M:%S %z')

    printf '%s\n\n' 'Rasterfall project index' >"$temporary"
    printf 'Git commit: %s\n' "$git_hash" >>"$temporary"
    printf 'Generated at: %s\n\n' "$generated_at" >>"$temporary"

    printf '%s\n' 'Read docs/rasterfall/README.md first, then select collections by task.' >>"$temporary"
    printf '%s\n\n' 'Files occur in one source collection; cross-module dependencies remain in companion collections.' >>"$temporary"
    printf '%s\n' 'Source collections (output bytes, source lines, files, output):' >>"$temporary"
    cat "$GROUP_STATS" >>"$temporary"
    printf '\n%s\n' 'Companion collections by task:' >>"$temporary"
    printf '%s\n' \
        'Gameplay/session/network: gameplay + maps + runtime; platform for sockets.' \
        'World rendering/lighting: rendering + platform + maps; runtime for submission.' \
        'Character/animation: characters + rendering + platform; gameplay for action semantics.' \
        'UI/input/story: runtime + gameplay; rendering for Scene overlay.' \
        'Validation/capture: diagnostics + the affected module collections.' >>"$temporary"
    printf '\n%s\n' 'Collection responsibilities:' >>"$temporary"
    for group in $SOURCE_GROUPS; do
        printf '%s: %s\n' "$group" "$(group_title "$group")" >>"$temporary"
    done
    printf '\n%s\n' 'Generated arrays indexed only (bytes, lines, path):' >>"$temporary"
    cat "$GENERATED_STATS" >>"$temporary"
    printf '%s\n' 'Use shader sources and tools/generate_gpu_graphics_spirv.py; the array is not copied.' >>"$temporary"
    printf '\n%s\n' 'File ownership (source lines, path, collection, source bytes):' >>"$temporary"
    cat "$SOURCE_STATS" >>"$temporary"

    printf '\n%s\n' 'Rasterfall source and asset tree:' >>"$temporary"
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

for group in $SOURCE_GROUPS; do
    output="$OUTPUT_DIR/rasterfall-source-$group.txt"
    write_snapshot "$output" "Rasterfall: $(group_title "$group")" "$WORK_DIR/$group-list" 1
    awk -F '\t' -v group="$group" -v bytes="$(wc -c <"$output")" -v output="${output##*/}" \
        '$3 == group { lines += $1; count++ } END { printf "%s\t%d\t%d\t%s\n", bytes, lines, count, output }' \
        "$SOURCE_STATS" >>"$GROUP_STATS"
    printf 'Wrote %s\n' "$output"
done
write_project_index
write_snapshot "$DOC_OUTPUT" 'Rasterfall documentation' "$DOC_LIST" 0

rm -f "$OUTPUT_DIR/rasterfall-source.txt" "$OUTPUT_DIR/rasterfall-source-and-headers.txt"
printf 'Wrote %s\nWrote %s\n' "$PROJECT_OUTPUT" "$DOC_OUTPUT"
