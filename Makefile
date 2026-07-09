#
# Makefile — tcc (ToyCCompiler) 构建系统
#
# 用法：
#   make             构建
#   make test        运行常规测试
#   make test-selfhost  运行自包含测试
#
# tcc 还处于早期开发阶段，目前用 gcc 编译。
# 自举种子详见 bootstrap/ 目录，用于验证自举收敛性。
#

# ─── 工具链 ────────────────────────────────────────────────────

CC      ?= gcc
LD      ?= ld

# ─── 标志 ───────────────────────────────────────────────────────

CFLAGS  := -nostdlib -ffreestanding -Wall -Wextra -I include -I app
LDFLAGS := -nostdlib -static -e __tlibc_start

# ─── 路径 ───────────────────────────────────────────────────────

BUILD   := build
SRC     := app
INC     := include

# ─── 头文件依赖（修改后触发增量编译） ─────────────────────────

TCC_NEED := $(INC)/tcc_need.h
ELF_H    := $(INC)/elf.h
ELF_W_H  := $(SRC)/elf_write.h
HEADERS  := $(TCC_NEED) $(ELF_H) $(ELF_W_H)

# ─── 默认目标 ──────────────────────────────────────────────────

.PHONY: all clean test test-selfhost test-source test-source-tcc

all: $(BUILD)/tcc $(BUILD)/tas
	@printf "$(GREEN)✓ 构建完成$(RESET)\n"

# ─── 源文件分组 ────────────────────────────────────────────────

# tcc 编译器
TCC_C_SRCS := tcc.c lex.c parse.c preproc.c cgen.c cgen_expr.c \
              cgen_asm.c elf_write.c tcc_rt.c
TCC_C_OBJS := $(addprefix $(BUILD)/, $(TCC_C_SRCS:.c=.o))
TCC_OBJS   := $(TCC_C_OBJS) $(BUILD)/tcc_rt_start.o

# tas 汇编器
TAS_C_SRCS := tas.c elf_write.c tcc_rt.c
TAS_C_OBJS := $(addprefix $(BUILD)/, $(TAS_C_SRCS:.c=.o))
TAS_OBJS   := $(TAS_C_OBJS) $(BUILD)/tcc_rt_start.o

# tpp 预处理器
TPP_C_SRCS := tpp.c preproc.c tcc_rt.c
TPP_C_OBJS := $(addprefix $(BUILD)/, $(TPP_C_SRCS:.c=.o))
TPP_OBJS   := $(TPP_C_OBJS) $(BUILD)/tcc_rt_start.o

ALL_OBJS := $(sort $(TCC_OBJS) $(TAS_OBJS) $(TPP_OBJS))

# ─── 目录创建 ──────────────────────────────────────────────────

$(BUILD):
	mkdir -p $(BUILD)

# ─── 汇编入口 ──────────────────────────────────────────────────

$(BUILD)/tcc_rt_start.o: $(SRC)/tcc_rt_start.S | $(BUILD)
	@printf "  $(BLUE)  AS$(RESET)  %s\n" "$<"
	$(CC) -x assembler-with-cpp -c $< -o $@

# ─── C 编译规则 ────────────────────────────────────────────────

$(BUILD)/%.o: $(SRC)/%.c $(HEADERS) | $(BUILD)
	@printf "  $(BLUE)  CC$(RESET)  %-20s " "$<"
	$(CC) $(CFLAGS) -MD -c $< -o $@
	@printf "$(GREEN)ok$(RESET)\n"

# parse.c：无 #include 行，强制包含 tcc.h
$(BUILD)/parse.o: $(SRC)/parse.c $(HEADERS) | $(BUILD)
	@printf "  $(BLUE)  CC$(RESET)  %-20s " "$<"
	$(CC) $(CFLAGS) -include tcc.h -MD -c $< -o $@
	@printf "$(GREEN)ok$(RESET)\n"

# tcc_rt.c：自包含运行时，仅依赖 tcc_need.h
$(BUILD)/tcc_rt.o: $(SRC)/tcc_rt.c $(TCC_NEED) | $(BUILD)
	@printf "  $(BLUE)  CC$(RESET)  %-20s " "$<"
	$(CC) $(CFLAGS) -MD -c $< -o $@
	@printf "$(GREEN)ok$(RESET)\n"

# ─── 链接规则 ──────────────────────────────────────────────────

$(BUILD)/tcc: $(TCC_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tcc ... "
	$(LD) $(LDFLAGS) $^ -o $@
	@size=$$(stat -c%s $@); printf "$(GREEN)ok$(RESET) ($$size bytes)\n"

$(BUILD)/tas: $(TAS_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tas ... "
	$(LD) $(LDFLAGS) $^ -o $@
	@printf "$(GREEN)ok$(RESET)\n"

$(BUILD)/tpp: $(TPP_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tpp ... "
	$(LD) $(LDFLAGS) $^ -o $@
	@printf "$(GREEN)ok$(RESET)\n"

# ─── 清理 ──────────────────────────────────────────────────────

clean:
	@printf "$(BLUE)  CLEAN$(RESET) 删除构建产物 ... "
	rm -rf $(BUILD) tmp
	@printf "$(GREEN)done$(RESET)\n"

# ─── 依赖文件包含（-MD 自动生成的 .d 实现增量头文件跟踪） ──
-include $(ALL_OBJS:.o=.d)

# ─── 测试 ──────────────────────────────────────────────────────

RED    := \033[31m
GREEN  := \033[32m
YELLOW := \033[33m
BLUE   := \033[34m
RESET  := \033[0m

TESTDIR     := compiler-tests/basic
SELFTESTDIR := compiler-tests/selfhost
LDTESTFLAGS := -nostdlib -static -T ld.script

# 常规测试
test: $(BUILD)/tcc $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	ids=$(filter-out test test-all test-selfhost test-source test-source-tcc,$(MAKECMDGOALS)); \
	if [ -z "$$ids" ]; then files="$(TESTDIR)/*.c"; \
	else files=; for n in $$ids; do files="$$files $(TESTDIR)/$${n}_*.c"; done; fi; \
	printf "$(BLUE)══════ tcc 常规测试 ══════$(RESET)\n"; \
	if [ -n "$$ids" ]; then printf "  指定编号: $$ids\n"; fi; \
	printf "\n"; \
	for f in $$files; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  tcc → $(BLUE)%-26s$(RESET) " "$$name"; \
		$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>/dev/null || { printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(LD) $(LDTESTFLAGS) /tmp/$$name.o $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o -o /tmp/$$name 2>/dev/null || { printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else printf "$(RED)✗$(RESET) (want %d got %d) — tmp/$$name.log\n" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# 自包含测试（无 tcc_rt 依赖）
test-selfhost: $(BUILD)/tcc
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ tcc 自包含测试 ══════$(RESET)\n\n"; \
	for f in $(SELFTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  tcc → $(BLUE)%-26s$(RESET) " "$$name"; \
		$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>/dev/null || { printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(LD) $(LDTESTFLAGS) /tmp/$$name.o -o /tmp/$$name 2>/dev/null || { printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else printf "$(RED)✗$(RESET) (want %d got %d) — tmp/$$name.log\n" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# ─── 源文件独立测试 ───────────────────────────────────────────

SOURCETESTDIR := compiler-tests/source
SCTEST_CC       ?= gcc
SCTEST_CFLAGS   ?= -nostdlib -ffreestanding -Wall -Wextra -O0
SCTEST_LDFLAGS  := -nostdlib -static -T ld.script

ifeq ($(SCTEST_USE_TCC),)
test-source: $(SOURCETESTDIR)/lex.c
else
test-source: $(BUILD)/tcc
endif
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ source 测试（$(if $(SCTEST_USE_TCC),tcc,gcc) 编译）══════$(RESET)\n\n"; \
	for f in $(SOURCETESTDIR)/*.c; do \
		name=$$(basename "$$f" .c); \
		total=$$((total+1)); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  $(BLUE)%-25s$(RESET) " "$$name"; \
		if [ -n "$(SCTEST_USE_TCC)" ]; then \
			$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>tmp/test_source_$$name-compile.log; \
			if [ $$? -ne 0 ]; then \
				printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; \
			fi; \
			$(LD) $(SCTEST_LDFLAGS) /tmp/$$name.o -o /tmp/test_$$name 2>tmp/test_source_$$name-link.log; \
			if [ $$? -ne 0 ]; then \
				printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; \
			fi; \
		else \
			$(SCTEST_CC) $(SCTEST_CFLAGS) -Wl,-e,__tlibc_start "$$f" -o /tmp/test_$$name 2>tmp/test_source_$$name-compile.log; \
			if [ $$? -ne 0 ]; then \
				printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; \
			fi; \
		fi; \
		/tmp/test_$$name > tmp/test_source_$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then \
			printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else \
			printf "$(RED)✗$(RESET) (want %d got %d) — tmp/test_source_$$name.log\n" "$$expect" "$$got"; fail=$$((fail+1)); \
		fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

test-source-tcc: $(BUILD)/tcc
	$(MAKE) test-source SCTEST_USE_TCC=1

# ─── 全部测试 ──────────────────────────────────────────────────

test-all: test test-selfhost test-source test-source-tcc
	@printf "$(GREEN)✓ 全部测试通过$(RESET)\n"

# ─── 允许 make test 01 05 等带编号参数 ────────────────────────

%:
	@:
