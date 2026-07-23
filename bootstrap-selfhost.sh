#!/bin/bash
#
# bootstrap-selfhost.sh — toyc 自举自托管测试
#
# 流程：
#   1. 用自举种子 bootstrap/toyc + bootstrap/toyas 构建 stage-2 toyc
#   2. 用 stage-2 toyc 编译、链接、运行 selfhost 测试
#
# 全部日志输出到 tmp/ 目录，stage-2 的日志带 stage2_ 前缀。
# 退出码：0 = 全部通过，1 = 有失败。

# ─── 颜色 ────────────────────────────────────────────────────────────
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
BLUE='\033[34m'
RESET='\033[0m'

# ─── 路径 ────────────────────────────────────────────────────────────
BUILD="build"
SRC="compiler"
INC="include"
TMP="tmp"
SELFTESTDIR="compiler-tests/selfhost"
STAGE2_DIR="${BUILD}/stage2"
BOOTSTRAP="bootstrap"
SEED_TOYC="${BOOTSTRAP}/toyc"
SEED_TOYAS="${BOOTSTRAP}/toyas"

mkdir -p "${TMP}" "${STAGE2_DIR}"

# ─── 工具链 ────────────────────────────────────────────────────────
LD="ld"
LDFLAGS="-nostdlib -static -e __tlibc_start"
SELFTEST_LDFLAGS="-nostdlib -static -T ld.script"

printf "${BLUE}╔══════════════════════════════════════════════════════════╗${RESET}\n"
printf "${BLUE}║        toyc 自举自托管测试脚本（Bootstrap + Selfhost）     ║${RESET}\n"
printf "${BLUE}╚══════════════════════════════════════════════════════════╝${RESET}\n\n"

# ─── 第 1 步：确认自举种子可用 ─────────────────────────────────

printf "${BLUE}=== [1/3] 确认自举种子（${SEED_TOYC} + ${SEED_TOYAS}） ===${RESET}\n"
if [ ! -x "${SEED_TOYC}" ] || [ ! -x "${SEED_TOYAS}" ]; then
    printf "  ${RED}ERROR${RESET} 自举种子不存在，请先运行 'make seed' 构建种子。\n"
    printf "  需要: ${SEED_TOYC} 和 ${SEED_TOYAS}\n"
    exit 1
fi
printf "  ${GREEN}✓${RESET} Seed toyc: ${SEED_TOYC} ($(ls -lh "${SEED_TOYC}" | awk '{print $5}'))\n"
printf "  ${GREEN}✓${RESET} Seed toyas: ${SEED_TOYAS} ($(ls -lh "${SEED_TOYAS}" | awk '{print $5}'))\n"
printf "\n"

# ─── 第 2 步：用种子 toyc 编译自身 → stage-2 toyc ─────────────

printf "${BLUE}=== [2/3] 构建 stage-2 toyc（seed tcc 编译自身 → ${BUILD}/toyc-stage2） ===${RESET}\n"

# 需要编译的 C 源文件（只编译 toyc 本身所需的模块；toypp 和 toyas 是独立工具，
# 它们有自己的 main() 和重复符号，不能混入同一个链接。）
STAGE2_C_FILES="${SRC}/toyc.c ${SRC}/lex.c ${SRC}/parse.c ${SRC}/preproc.c ${SRC}/cgen.c ${SRC}/cgen_expr.c ${SRC}/cgen_float_hack.c ${SRC}/cgen_asm.c ${SRC}/elf_write.c ${SRC}/toyc_rt.c"

# 用种子 toyc 编译每个 C 源文件
COMPILE_FAILED=0
for cfile in ${STAGE2_C_FILES}; do
    basename_c=$(basename "${cfile}" .c)
    ofile="${STAGE2_DIR}/${basename_c}.o"

    printf "  ${BLUE}编译${RESET} %s → %s ... " "${cfile}" "${ofile}"

    if ${SEED_TOYC} "${cfile}" -o "${ofile}" 2>"${TMP}/stage2_compile_${basename_c}.log"; then
        printf "${GREEN}ok${RESET}\n"
    else
        printf "${RED}FAIL${RESET}（日志: ${TMP}/stage2_compile_${basename_c}.log）\n"
        COMPILE_FAILED=1
    fi
done

# 编译 toyc_rt_start.S（汇编文件，用种子 toyas 汇编）
printf "  ${BLUE}[toyas]${RESET} %s → %s ... " "toyc_rt_start.S" "${STAGE2_DIR}/toyc_rt_start.o"
if ${SEED_TOYAS} "${SRC}/toyc_rt_start.S" -o "${STAGE2_DIR}/toyc_rt_start.o" 2>"${TMP}/stage2_compile_tcc_rt_start.log"; then
    printf "${GREEN}ok${RESET}\n"
else
    printf "${RED}FAIL${RESET}（日志: ${TMP}/stage2_compile_tcc_rt_start.log）\n"
    COMPILE_FAILED=1
fi

if [ "${COMPILE_FAILED}" -eq 1 ]; then
    printf "\n  ${RED}✗ stage-2 编译失败，检查日志文件。${RESET}\n"
    printf "    编译日志在 ${TMP}/stage2_compile_*.log\n\n"
    exit 1
fi

# 链接 stage-2 toyc
printf "\n  链接 stage-2 toyc ... "
STAGE2_OBJS="${STAGE2_DIR}/toyc.o ${STAGE2_DIR}/lex.o ${STAGE2_DIR}/parse.o ${STAGE2_DIR}/preproc.o ${STAGE2_DIR}/cgen.o ${STAGE2_DIR}/cgen_expr.o ${STAGE2_DIR}/cgen_float_hack.o ${STAGE2_DIR}/cgen_asm.o ${STAGE2_DIR}/elf_write.o ${STAGE2_DIR}/toyc_rt.o ${STAGE2_DIR}/toyc_rt_start.o"

if ${LD} ${LDFLAGS} ${STAGE2_OBJS} -o "${BUILD}/toyc-stage2" 2>"${TMP}/stage2_link.log"; then
    printf "${GREEN}ok${RESET} → ${BUILD}/toyc-stage2\n"
    ls -lh "${BUILD}/toyc-stage2" | awk '{print "    size:", $5}'
else
    printf "${RED}FAIL${RESET}（日志: ${TMP}/stage2_link.log）\n"
    cat "${TMP}/stage2_link.log" | sed 's/^/    /'
    printf "\n  ${RED}✗ stage-2 链接失败，跳过测试。${RESET}\n\n"
    exit 1
fi

STAGE2_TCC="${BUILD}/toyc-stage2"
printf "\n"

# ─── 第 3 步：用 stage-2 toyc 运行 selfhost 测试 ─────────────────

printf "${BLUE}=== [3/3] stage-2 toyc 运行 selfhost 测试 ===${RESET}\n\n"

ok=0
fail=0
total=0
stage2_log_dir="${TMP}/stage2_selfhost"
mkdir -p "${stage2_log_dir}"

for f in ${SELFTESTDIR}/*.c; do
    [ -f "${f}" ] || continue

    total=$((total + 1))
    name=$(basename "${f}" .c)
    expect=$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "${f}" | head -1)
    [ -z "${expect}" ] && expect=0

    printf "  [stage2] ${BLUE}%s${RESET} ... " "${name}"

    # 编译
    if ! ${STAGE2_TCC} "${f}" -o "/tmp/stage2_${name}.o" 2>"${stage2_log_dir}/${name}-compile.log"; then
        printf "${RED}COMPILE FAIL${RESET}\n"
        fail=$((fail + 1))
        continue
    fi

    # 链接
    if ! ${LD} ${SELFTEST_LDFLAGS} "/tmp/stage2_${name}.o" -o "/tmp/stage2_${name}" 2>"${stage2_log_dir}/${name}-link.log"; then
        printf "${RED}LINK FAIL${RESET}\n"
        fail=$((fail + 1))
        continue
    fi

    # 运行
    "/tmp/stage2_${name}" > "${stage2_log_dir}/${name}.log" 2>&1
    got=$?

    if [ "${got}" = "${expect}" ]; then
        printf "${GREEN}ok${RESET} (%d)\n" "${got}"
        ok=$((ok + 1))
    else
        printf "${RED}FAIL${RESET} (want %d got %d) — log: ${stage2_log_dir}/${name}.log\n" "${expect}" "${got}"
        fail=$((fail + 1))
    fi
done

# ─── 汇总 ──────────────────────────────────────────────────────────

printf "\n${BLUE}═══════════════════════════════════════════════════════════${RESET}\n"
printf "${BLUE}  种子自举测试结果${RESET}\n"
printf "${BLUE}═══════════════════════════════════════════════════════════${RESET}\n"
printf "  种子 toyc:       ${SEED_TOYC}\n"
printf "  种子 toyas:       ${SEED_TOYAS}\n"
printf "  ${GREEN}%d passed${RESET}, ${RED}%d failed${RESET}, %d total\n" "${ok}" "${fail}" "${total}"
printf "  Stage-2 toyc:     ${BUILD}/toyc-stage2\n"
printf "  Stage-2 目标文件: ${STAGE2_DIR}/\n"
printf "  编译日志:        ${TMP}/stage2_compile_*.log\n"
printf "  链接日志:        ${TMP}/stage2_link.log\n"
printf "  测试日志:        ${stage2_log_dir}/*.log\n"
printf "${BLUE}═══════════════════════════════════════════════════════════${RESET}\n\n"

# ─── exit code ───────────────────────────

if [ "${fail}" -gt 0 ]; then
    printf "${RED}✗ 有 ${fail} 个测试失败${RESET}\n"
    exit 1
fi
printf "${GREEN}✓ 全部通过${RESET}\n"
exit 0
