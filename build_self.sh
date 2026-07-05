#!/bin/bash
#
# build_self.sh — 用 tcc 编译 tcc 自身（自举脚本）
#
# 策略：
#   1. 用 gcc 汇编 tcc_rt_start.S — tcc 不支持汇编，这是唯一的外部依赖
#   2. 用当前 build/tcc 编译 tcc_rt.c 和各模块源文件 → .o
#   3. 用 ld 链接 → build-self/tcc
#   4. 可选：用新 tcc 再编译一遍所有源文件（二次自举验证）
#
# 关于 tpp/tas：tcc 尚不内联 static inline 函数（见 tcc.h 中的 e1() 等），
# 导致预处理器模块 preproc.o 携带不必要的代码生成引用，无法独立链接。
# 这是 tcc 自身的代码生成限制，随自举推进逐步修复。当前脚本专注于 tcc。
#
# 用法：
#   ./build_self.sh                   # 自举：tcc → build-self/tcc
#   ./build_self.sh --stage2          # 自举 + 二次自举验证
#   ./build_self.sh --clean           # 清理 build-self 目录
#   ./build_self.sh --tcc=build/xxx   # 指定 tcc 路径（默认 build/tcc）
#
# 注意：必须从项目根目录运行。

set -u

# ─── 配置 ────────────────────────────────────────────────────────

TCC="./build/tcc"
LD="${LD:-ld}"
LDFLAGS="${LDFLAGS:--nostdlib -static -e __tlibc_start}"
BUILD="${BUILD:-build-self}"
CC="${CC:-gcc}"

RT_SRC="app/tcc_rt.c"
RT_ASM="app/tcc_rt_start.S"

# tcc 编译器模块（按链接顺序排列）
TCC_SRCS="
    app/tcc.c      app/lex.c      app/parse.c
    app/preproc.c  app/cgen.c     app/cgen_expr.c
    app/cgen_asm.c app/elf_write.c
"

TCC_OBJS="
    tcc.o tcc_rt.o tcc_rt_start.o
    lex.o parse.o preproc.o
    cgen.o cgen_expr.o cgen_asm.o elf_write.o
"

# ─── 辅助函数 ────────────────────────────────────────────────────

G="\033[32m" R="\033[31m" Y="\033[33m" B="\033[34m" N="\033[0m"
log()  { printf "  ${G}->${N} %s\n" "$*"; }
ok()   { printf "  ${G}OK${N}   %s\n" "$*"; }
err()  { printf "  ${R}FAIL${N} %s\n" "$*"; }
warn() { printf "  ${Y}WARN${N} %s\n" "$*"; }
title(){ printf "\n${B}=== %s ===${N}\n" "$*"; }

xcompile() {
    local tcc="$1" src="$2" out="$3"
    printf "  ${G}tcc:${N} %-30s → %s\n" "$(basename "$src")" "$(basename "$out")"
    if ! "$tcc" "$src" -o "$out" 2>/tmp/tcc_self_err.log; then
        err "$(basename "$src") 编译失败"
        sed 's/^/    /' /tmp/tcc_self_err.log
        return 1
    fi
    return 0
}

xasm() {
    log "as:    $(basename "$1") → $(basename "$2")"
    "$CC" -x assembler-with-cpp -c "$1" -o "$2"
}

xlink() {
    local out="$1"; shift
    log "ld:    $(basename "$out")"
    "$LD" $LDFLAGS "$@" -o "$out"
}

cleanup() {
    log "清理 $BUILD/"
    rm -rf "$BUILD"
}

# ─── 参数 ────────────────────────────────────────────────────────

DO_STAGE2=0
for arg in "$@"; do
    case "$arg" in
        --clean)  cleanup; exit 0 ;;
        --stage2) DO_STAGE2=1 ;;
        --help)   sed -n '2,20p' "$0"; exit 0 ;;
        --tcc=*)  TCC="${arg#--tcc=}" ;;
    esac
done

# ─── 前置检查 ────────────────────────────────────────────────────

if [ ! -f "$TCC" ]; then
    err "找不到 tcc: $TCC  （请先 make）"
    exit 1
fi
if [ ! -f "app/tcc.c" ]; then
    err "请在项目根目录运行"
    exit 1
fi
mkdir -p "$BUILD"

# ═══════════════════════════════════════════════════════════════════
#  Stage 1：用现有 build/tcc 编译自己
# ═══════════════════════════════════════════════════════════════════

title "Stage 1 — 运行时（唯一的外部依赖：tcc_rt_start.S 需 gcc 汇编）"

xasm  "$RT_ASM"  "$BUILD/tcc_rt_start.o"
xcompile "$TCC" "$RT_SRC" "$BUILD/tcc_rt.o"

title "Stage 1 — 编译 tcc 的所有 8 个 C 模块"

ALL_SRC_OK=1
for src in $TCC_SRCS; do
    name="$(basename "$src" .c)"
    if xcompile "$TCC" "$src" "$BUILD/$name.o"; then
        :
    else
        ALL_SRC_OK=0
    fi
done

if [ "$ALL_SRC_OK" = "0" ]; then
    err "有模块编译失败，终止"
    exit 1
fi

title "Stage 1 — 链接 tcc"

LINK_OBJS=""
for obj in $TCC_OBJS; do
    LINK_OBJS="$LINK_OBJS $BUILD/$obj"
done
xlink "$BUILD/tcc" $LINK_OBJS

SIZE1=$(stat -c%s "$BUILD/tcc")
log "stage1 tcc: $BUILD/tcc （${SIZE1} bytes）"

# ─── 快速验证 ────────────────────────────────────────────────────

title "Stage 1 — 验证（用 stage1 tcc 编译 selfhost 测试）"

SIMPLE_TEST="compiler-tests/selfhost/01_totally_selfcontained.c"
STAGE1_WORKS=0
if [ -f "$SIMPLE_TEST" ]; then
    set +e
    "$BUILD/tcc" "$SIMPLE_TEST" -o "/tmp/self_stage1_test.o" 2>/tmp/tcc_verify.log
    RC=$?
    set -e
    if [ $RC -eq 0 ]; then
        ok "$(basename $SIMPLE_TEST) 编译成功 — 自举进展良好！"
        STAGE1_WORKS=1
    else
        warn "stage1 tcc 编译测试文件退出码 $RC（自举是渐进过程）"
    fi
fi

# ═══════════════════════════════════════════════════════════════════
#  Stage 2（可选）：用 stage1 的 tcc 再编译一遍所有源文件
# ═══════════════════════════════════════════════════════════════════

if [ "$DO_STAGE2" = "1" ]; then
    if [ "$STAGE1_WORKS" != "1" ]; then
        warn "stage1 tcc 尚未能正确编译代码，跳过 stage2"
    else
        STAGE2="$BUILD/stage2"
        mkdir -p "$STAGE2"

        title "Stage 2 — 用 stage1 tcc 二次编译（运行时）"

        xasm  "$RT_ASM"  "$STAGE2/tcc_rt_start.o"
        xcompile "$BUILD/tcc" "$RT_SRC" "$STAGE2/tcc_rt.o"

        title "Stage 2 — 编译 8 个模块"

        for src in $TCC_SRCS; do
            name="$(basename "$src" .c)"
            xcompile "$BUILD/tcc" "$src" "$STAGE2/$name.o"
        done

        title "Stage 2 — 链接"

        S2_OBJS=""
        for obj in $TCC_OBJS; do
            S2_OBJS="$S2_OBJS $STAGE2/$obj"
        done
        xlink "$STAGE2/tcc" $S2_OBJS

        SIZE2=$(stat -c%s "$STAGE2/tcc")
        log "stage2 tcc: $STAGE2/tcc （${SIZE2} bytes）"

        title "Stage 2 — 二进制比较"
        log "stage1: $SIZE1 bytes"
        log "stage2: $SIZE2 bytes"

        if cmp -s "$BUILD/tcc" "$STAGE2/tcc" 2>/dev/null; then
            ok "完全一致 — tcc 已能正确编译自身！"
        elif [ "$SIZE1" = "$SIZE2" ]; then
            warn "大小相同但内容不同（可能是重定位偏移差异）"
        else
            warn "大小不同（stage1=$SIZE1  stage2=$SIZE2）"
        fi
    fi
fi

# ═══════════════════════════════════════════════════════════════════
#  结果
# ═══════════════════════════════════════════════════════════════════

title "完成"
ok "build-self/tcc — ${SIZE1} bytes"
echo ""
echo "  由 $(basename $TCC) 编译自身源文件，ld 静态链接。"
echo "  下一步：修复 tcc 的代码生成 bug 直至 stage1 tcc 能编译测试文件。"
echo ""
