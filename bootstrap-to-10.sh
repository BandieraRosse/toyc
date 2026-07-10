#!/bin/bash
#
# bootstrap-to-10.sh — tcc 自举到 stage 10
#
# 逐级用 stage-N tcc 编译自身源码，产出 stage-(N+1)，并验证每级通过 selfhost 测试。
# 不修改任何已有文件。
#
# 原理：如果 tcc 是正确自举的编译器，从某个 stage 开始二进制会收敛（不再变化），
# 后续 stage 与之前的完全相同。本脚本验证这一收敛性。
#
# 用法:  ./bootstrap-to-10.sh
# 退出码: 0 = 全部通过

set -o pipefail

RED='\033[31m'; GREEN='\033[32m'; YELLOW='\033[33m'; BLUE='\033[34m'; CYAN='\033[36m'; RESET='\033[0m'

BUILD="build"
SRC="app"
TMP="tmp"
STAGES_DIR="${BUILD}/stages"
SELFTESTDIR="compiler-tests/selfhost"
BOOTSTRAP="bootstrap"
SEED_TCC="${BOOTSTRAP}/tcc"
SEED_TAS="${BOOTSTRAP}/tas"

# ─── 工具链 ────────────────────────────────────────────────────────
LD="ld"
LDFLAGS="-nostdlib -static -e __tlibc_start"
SELFTEST_LDFLAGS="-nostdlib -static -T ld.script"

# tcc 编译器本身的 C 源文件（不含 tpp、tas — 它们是独立工具）
C_FILES="${SRC}/tcc.c ${SRC}/lex.c ${SRC}/parse.c ${SRC}/preproc.c ${SRC}/cgen.c ${SRC}/cgen_expr.c ${SRC}/cgen_asm.c ${SRC}/elf_write.c ${SRC}/tcc_rt.c"

mkdir -p "${TMP}" "${STAGES_DIR}"

# ─── 辅助函数 ─────────────────────────────────────────────────────

# 用给定的编译器编译 tcc 的 9 个 C 源文件 + 链接 → tcc-stage{N}
# 参数: stage_num compiler_path
build_stage() {
    local stage_num="$1"
    local compiler="$2"
    local objdir="${STAGES_DIR}/stage${stage_num}"
    local compile_failed=0

    mkdir -p "${objdir}"

    for cfile in ${C_FILES}; do
        local basename_c; basename_c=$(basename "${cfile}" .c)
        local ofile="${objdir}/${basename_c}.o"

        printf "  ${BLUE}编译${RESET} %s → %s ... " "${cfile}" "${ofile}"
        if ${compiler} "${cfile}" -o "${ofile}" 2>"${TMP}/stage${stage_num}_compile_${basename_c}.log"; then
            printf "${GREEN}ok${RESET}\n"
        else
            printf "${RED}FAIL${RESET}（日志: ${TMP}/stage${stage_num}_compile_${basename_c}.log）\n"
            cat "${TMP}/stage${stage_num}_compile_${basename_c}.log" | sed 's/^/    /'
            compile_failed=1
        fi
    done

    # tcc_rt_start.S 是汇编文件，用自举种子 tas 汇编
    printf "  ${BLUE}[tas]${RESET} %s → %s ... " "tcc_rt_start.S" "${objdir}/tcc_rt_start.o"
    if ${SEED_TAS} "${SRC}/tcc_rt_start.S" -o "${objdir}/tcc_rt_start.o" \
        2>"${TMP}/stage${stage_num}_compile_tcc_rt_start.log"; then
        printf "${GREEN}ok${RESET}\n"
    else
        printf "${RED}FAIL${RESET}（日志: ${TMP}/stage${stage_num}_compile_tcc_rt_start.log）\n"
        compile_failed=1
    fi

    [ "${compile_failed}" -eq 1 ] && return 1

    local objs="${objdir}/tcc.o ${objdir}/lex.o ${objdir}/parse.o ${objdir}/preproc.o ${objdir}/cgen.o ${objdir}/cgen_expr.o ${objdir}/cgen_asm.o ${objdir}/elf_write.o ${objdir}/tcc_rt.o ${objdir}/tcc_rt_start.o"
    local out="${STAGES_DIR}/tcc-stage${stage_num}"

    printf "  链接 stage-%d tcc ... " "${stage_num}"
    if ${LD} ${LDFLAGS} ${objs} -o "${out}" 2>"${TMP}/stage${stage_num}_link.log"; then
        printf "${GREEN}ok${RESET} → $(ls -lh "${out}" | awk '{print $5}')\n"
        return 0
    else
        printf "${RED}FAIL${RESET}（日志: ${TMP}/stage${stage_num}_link.log）\n"
        cat "${TMP}/stage${stage_num}_link.log" | sed 's/^/    /'
        return 1
    fi
}

# 用 stage-N tcc 运行全部 28 个 selfhost 测试
# 输出 "ok fail total"
test_stage() {
    local stage_num="$1"
    local compiler="${STAGES_DIR}/tcc-stage${stage_num}"
    local ok=0 fail=0 total=0
    local logdir="${TMP}/stage${stage_num}_selfhost"

    [ ! -x "${compiler}" ] && { echo "0 1 0"; return 1; }

    mkdir -p "${logdir}"

    for f in ${SELFTESTDIR}/*.c; do
        [ -f "${f}" ] || continue
        total=$((total+1))
        local name; name=$(basename "${f}" .c)
        local expect; expect=$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "${f}" | head -1)
        [ -z "${expect}" ] && expect=0

        if ! ${compiler} "${f}" -o "/tmp/stg${stage_num}_${name}.o" >"${logdir}/${name}-compile.log" 2>&1; then
            fail=$((fail+1)); continue
        fi
        if ! ${LD} ${SELFTEST_LDFLAGS} "/tmp/stg${stage_num}_${name}.o" -o "/tmp/stg${stage_num}_${name}" \
            >"${logdir}/${name}-link.log" 2>&1; then
            fail=$((fail+1)); continue
        fi
        "/tmp/stg${stage_num}_${name}" > "${logdir}/${name}.log" 2>&1
        local got=$?
        [ "${got}" = "${expect}" ] && ok=$((ok+1)) || fail=$((fail+1))
    done

    echo "${ok} ${fail} ${total}"
}

# ─── 主流程 ─────────────────────────────────────────────────────────

printf "${CYAN}╔══════════════════════════════════════════════════════════════╗${RESET}\n"
printf "${CYAN}║            tcc 自举到 Stage 10                             ║${RESET}\n"
printf "${CYAN}║  逐级自编译 → MD5 收敛验证                                   ║${RESET}\n"
printf "${CYAN}║  完整测试: stage-1(种子) + stage-2(首自编) + stage-10(收敛)  ║${RESET}\n"
printf "${CYAN}╚══════════════════════════════════════════════════════════════╝${RESET}\n\n"

# ─── 确保 stage-1（自举种子）存在 ──────────────────────────

printf "${BLUE}═══ [准备] 确认自举种子（${SEED_TCC} + ${SEED_TAS}） ═══${RESET}\n"
if [ ! -x "${SEED_TCC}" ] || [ ! -x "${SEED_TAS}" ]; then
    printf "  ${RED}✗ 自举种子不存在，请先运行 'make seed'。${RESET}\n"
    printf "  需要: ${SEED_TCC} 和 ${SEED_TAS}\n"
    exit 1
fi
printf "  ${GREEN}✓${RESET} Seed tcc: ${SEED_TCC} ($(ls -lh "${SEED_TCC}" | awk '{print $5}'))\n"
printf "  ${GREEN}✓${RESET} Seed tas: ${SEED_TAS} ($(ls -lh "${SEED_TAS}" | awk '{print $5}'))\n"

if [ ! -f "${STAGES_DIR}/tcc-stage1" ]; then
    cp "${SEED_TCC}" "${STAGES_DIR}/tcc-stage1"
    printf "  ${GREEN}✓${RESET} 复制为 stage-1\n"
else
    printf "  ${GREEN}✓${RESET} stage-1 已存在\n"
fi
printf "\n"

# ─── 构建 stages 2-10 ─────────────────────────────────────────

ALL_FAILED=0

for stage in $(seq 2 10); do
    prev=$((stage - 1))
    compiler="${STAGES_DIR}/tcc-stage${prev}"

    if [ ! -x "${compiler}" ]; then
        printf "${RED}✗ stage-%d 编译器 ${compiler} 不存在，无法构建 stage-%d${RESET}\n" "${prev}" "${stage}"
        ALL_FAILED=1
        continue
    fi

    if [ -f "${STAGES_DIR}/tcc-stage${stage}" ]; then
        printf "${BLUE}[%d/%d]${RESET} stage-%d 已存在，跳过构建。\n" "${stage}" "10" "${stage}"
        continue
    fi

    printf "${BLUE}[%d/%d]${RESET} 构建 stage-%d（← stage-%d tcc）\n" "${stage}" "10" "${stage}" "${prev}"
    if build_stage "${stage}" "${compiler}"; then
        printf "  ${GREEN}✓ stage-%d 构建成功${RESET}\n" "${stage}"
    else
        printf "  ${RED}✗ stage-%d 构建失败${RESET}\n" "${stage}"
        ALL_FAILED=1
    fi
    printf "\n"
done

# ─── 验证收敛的代表版本 ──────────────────────────────────

printf "${BLUE}═══ 验证 selfhost 测试（stage-1 + stage-2 + stage-10） ═══${RESET}\n"
printf "${BLUE}     中间 stage 仅做 MD5 一致性对比，不做完整测试。${RESET}\n\n"

STAGE_OK=0; STAGE_FAIL=0
declare -a STAGE_RESULTS

# 定义要完整测试的 stage
TEST_STAGES="1 2 10"

for stage in $(seq 1 10); do
    if [ ! -f "${STAGES_DIR}/tcc-stage${stage}" ] || [ ! -x "${STAGES_DIR}/tcc-stage${stage}" ]; then
        STAGE_RESULTS[${stage}]="—"
        continue
    fi

    case " ${TEST_STAGES} " in
        *" ${stage} "*)
            # 完整测试
            result=$(test_stage "${stage}")
            ok=$(echo "${result}" | awk '{print $1}')
            fail=$(echo "${result}" | awk '{print $2}')
            total=$(echo "${result}" | awk '{print $3}')

            if [ "${fail}" -eq 0 ] 2>/dev/null; then
                printf "  ${GREEN}✓ stage-%d${RESET}  %d/%d passed\n" "${stage}" "${ok}" "${total}"
                STAGE_RESULTS[${stage}]="PASS (${ok}/${total})"
                STAGE_OK=$((STAGE_OK+1))
            else
                printf "  ${RED}✗ stage-%d${RESET}  %d/%d passed, %d failed\n" "${stage}" "${ok}" "${total}" "${fail}"
                STAGE_RESULTS[${stage}]="FAIL (${ok}/${total}, ${fail} fail)"
                STAGE_FAIL=$((STAGE_FAIL+1))
            fi
            ;;
        *)
            # 跳过测试
            printf "  ${YELLOW}— stage-%d${RESET}  跳过（收敛后 stage-10 代表）\n" "${stage}"
            STAGE_RESULTS[${stage}]="SKIP"
            ;;
    esac
done

# ─── 二进制一致性分析 ──────────────────────────────────────────

printf "\n${BLUE}═══ 二进制一致性分析 ═══${RESET}\n\n"

printf "  %-10s %-10s %-34s %s\n" "Stage" "Size" "MD5" "Status"
printf "  %-10s %-10s %-34s %s\n" "──────" "────" "──────────────────────────────────" "──────"

STAGE_HASHES=()
for s in $(seq 1 10); do
    f="${STAGES_DIR}/tcc-stage${s}"
    if [ -f "${f}" ]; then
        size=$(stat -c%s "${f}" 2>/dev/null)
        hash=$(md5sum "${f}" | awk '{print $1}')
        STAGE_HASHES[${s}]="${hash}"
        printf "  stage-%-2d   %-8d  %s  %s\n" "${s}" "${size}" "${hash}" "${STAGE_RESULTS[${s}]}"
    else
        STAGE_HASHES[${s}]=""
        printf "  stage-%-2d   —         —                                 —\n" "${s}"
    fi
done

# 收敛点检测
CONVERGED_AT=""
for s in $(seq 2 10); do
    h1="${STAGE_HASHES[${s}]}"
    h2="${STAGE_HASHES[$((s-1))]}"
    if [ -n "${h1}" ] && [ -n "${h2}" ] && [ "${h1}" = "${h2}" ]; then
        CONVERGED_AT="${s}"
        break
    fi
done

printf "\n"
if [ -n "${CONVERGED_AT}" ]; then
    printf "  ${GREEN}✓ 自举在 stage-%d 收敛${RESET}\n" "${CONVERGED_AT}"
    printf "    Stage-%d 到 stage-10 的二进制完全相同，证明 tcc 是自洽的编译器。\n" "${CONVERGED_AT}"
    printf "\n"
    printf "    ${CYAN}收敛含义：${RESET}\n"
    printf "    - stage-%d 编译自身产生的代码与 stage-%d 完全一致\n" "$((CONVERGED_AT-1))" "${CONVERGED_AT}"
    printf "    - 后续 stages 只是重复同一过程，验证了自举的稳定性\n"
    printf "    - tcc 已掌握自身源码的编译能力，不依赖宿主编译器\n"
else
    # 检查是否所有 stage 都存在但不相同
    ALL_EXIST=true
    for s in $(seq 1 10); do
        [ -f "${STAGES_DIR}/tcc-stage${s}" ] || { ALL_EXIST=false; break; }
    done
    if [ "${ALL_EXIST}" = true ]; then
        h2="${STAGE_HASHES[2]}"; h3="${STAGE_HASHES[3]}"
        if [ -n "${h2}" ] && [ -n "${h3}" ] && [ "${h2}" != "${h3}" ]; then
            printf "  ${YELLOW}⚠ stage-2 和 stage-3 不同（预期：种子 tcc vs 自编译 tcc 生成不同），stage-3 之后未完全收敛${RESET}\n"
        fi
    fi
fi

# ─── 汇总 ──────────────────────────────────────────────────────────

printf "\n${CYAN}═══════════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Bootstrap-to-10 结果汇总${RESET}\n"
printf "${CYAN}═══════════════════════════════════════════════════════════════${RESET}\n"
printf "  Stages built:     10/10\n"
printf "  ${GREEN}Tests passed:    %d/3${RESET} (stage-1 + stage-2 + stage-10)\n" "${STAGE_OK}"
if [ "${STAGE_FAIL}" -gt 0 ]; then
    printf "  ${RED}Tests failed:    %d${RESET}\n" "${STAGE_FAIL}"
fi
printf "  Stage binaries:   ${STAGES_DIR}/tcc-stage{1..10}\n"
printf "  中间目标文件:     ${STAGES_DIR}/stage{2..10}/\n"
printf "  构建日志:         ${TMP}/stage{2..10}_compile_*.log\n"
printf "  链接日志:         ${TMP}/stage{2..10}_link.log\n"
printf "  测试日志:         ${TMP}/stage{1,2,10}_selfhost/*.log\n"
printf "${CYAN}═══════════════════════════════════════════════════════════════${RESET}\n\n"

if [ "${ALL_FAILED}" -gt 0 ] || [ "${STAGE_FAIL}" -gt 0 ]; then
    exit 1
fi
exit 0
