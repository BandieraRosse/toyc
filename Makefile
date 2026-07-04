#
# Makefile — tcc (Tinylibc C Compiler) 独立构建系统
#
# 从 app/ 编译 tcc/tpp/tas，输出到 build/
#
# 目标:
#   all       构建全部三个工具
#   install   复制输出到 build/（等于 all）
#   clean     删除构建产物
#   test      运行编译器测试套件（指定编号）
#   test-all  运行全部测试
#
# 变量（可覆盖）:
#   CC      宿主 C 编译器              (默认: gcc)
#   LD      宿主链接器                 (默认: ld)
#   CFLAGS  额外 C 编译标志
#   LDFLAGS 额外链接标志
#   BUILD   构建输出目录               (默认: build)
#

# ─── 工具链 ────────────────────────────────────────────────────

CC      ?= gcc
LD      ?= ld
CFLAGS  ?= -nostdlib -ffreestanding -Wall -Wextra
LDFLAGS ?= -nostdlib -static -e __tlibc_start

# ─── 路径 ───────────────────────────────────────────────────────

BUILD   := build
SRC     := app
INC     := include

CFLAGS  += -I $(INC) -I $(SRC)

# tcc 最小化头文件（修改后触发重新编译所有目标文件）
TCC_NEED := $(INC)/tcc_need.h
ELF_H    := $(INC)/elf.h
ELF_W_H  := $(SRC)/elf_write.h
HEADERS  := $(TCC_NEED) $(ELF_H) $(ELF_W_H)

# ─── 目标定义（按 tmakelist 分类） ────────────────────────────

# tcc: 完整 C 编译器
TCC_OBJS := $(BUILD)/tcc.o           \
            $(BUILD)/tcc_rt.o        \
            $(BUILD)/tcc_rt_start.o  \
            $(BUILD)/lex.o           \
            $(BUILD)/parse.o         \
            $(BUILD)/preproc.o       \
            $(BUILD)/cgen.o          \
            $(BUILD)/cgen_expr.o     \
            $(BUILD)/cgen_asm.o      \
            $(BUILD)/elf_write.o

# tpp: 独立预处理器
TPP_OBJS := $(BUILD)/tpp.o           \
            $(BUILD)/tcc_rt.o        \
            $(BUILD)/tcc_rt_start.o  \
            $(BUILD)/preproc.o

# tas: x86_64 汇编器
TAS_OBJS := $(BUILD)/tas.o           \
            $(BUILD)/tcc_rt.o        \
            $(BUILD)/tcc_rt_start.o  \
            $(BUILD)/elf_write.o

# ─── 目标文件（去重后供编译/清理用） ─────────────────────────

ALL_OBJS := $(sort $(TCC_OBJS) $(TPP_OBJS) $(TAS_OBJS))

# ─── 默认目标 ──────────────────────────────────────────────────

.PHONY: all install clean test test-all

all: $(BUILD)/tcc $(BUILD)/tpp $(BUILD)/tas

install: all
	@echo "  Build complete.  Output in $(BUILD)/:"
	@ls -lh $(BUILD)/tcc $(BUILD)/tpp $(BUILD)/tas

# ─── 创建 build 目录 ──────────────────────────────────────────

$(BUILD):
	mkdir -p $(BUILD)

# ─── C 编译规则 ────────────────────────────────────────────────
#
# 所有 .c 文件通过 tcc.h → tcc_need.h / elf.h 获取类型声明。
# parse.c 没有 #include，需要用 -include tcc.h 强制包含。
# tcc_rt.c 自包含，直接依赖 tcc_need.h。

# 通用规则：依赖头文件变化时重新编译
$(BUILD)/%.o: $(SRC)/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -MD -c $< -o $@

# parse.c: 没有 #include 行，强制包含 tcc.h
$(BUILD)/parse.o: $(SRC)/parse.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -include tcc.h -MD -c $< -o $@

# tcc_rt.c: 自包含运行时，仅依赖 tcc_need.h（不包含 tcc.h）
$(BUILD)/tcc_rt.o: $(SRC)/tcc_rt.c $(TCC_NEED) | $(BUILD)
	$(CC) $(CFLAGS) -MD -c $< -o $@

# ─── 汇编编译规则 ──────────────────────────────────────────────

$(BUILD)/tcc_rt_start.o: $(SRC)/tcc_rt_start.S | $(BUILD)
	$(CC) -x assembler-with-cpp -c $< -o $@

# ─── 链接规则 ──────────────────────────────────────────────────

$(BUILD)/tcc: $(TCC_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@
	@echo "  LD  $@"

$(BUILD)/tpp: $(TPP_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@
	@echo "  LD  $@"

$(BUILD)/tas: $(TAS_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@
	@echo "  LD  $@"

# ─── 清理 ──────────────────────────────────────────────────────

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d)
	rm -f $(BUILD)/tcc $(BUILD)/tpp $(BUILD)/tas
	rmdir $(BUILD) 2>/dev/null; true

# ─── 测试 ──────────────────────────────────────────────────────
#
# 用法：
#   make test           # 运行全部
#   make test 01 05     # 只跑指定编号
#
# 每个 compiler-tests/*.c 首行的 // EXPECT: N 指定预期退出码。
# 流程：tcc 编译 → ld 链接 → 运行并检查退出码。

RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
BLUE := \033[34m
RESET := \033[0m

TESTDIR := compiler-tests
LDTESTFLAGS := -nostdlib -static -T ld.script

test: $(BUILD)/tcc $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o
	@ok=0; fail=0; total=0; \
	ids=$(filter-out $@,$(MAKECMDGOALS)); \
	if [ -z "$$ids" ]; then files="$(TESTDIR)/*.c"; \
	else files=; for n in $$ids; do files="$$files $(TESTDIR)/$${n}_*.c"; done; fi; \
	printf "$(BLUE)=== tcc tests ===$(RESET)\n\n"; \
	for f in $$files; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>/dev/null || { printf "  $(RED)FAIL$(RESET) %s (compile)\n" "$$name"; fail=$$((fail+1)); continue; }; \
		$(LD) $(LDTESTFLAGS) /tmp/$$name.o $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o -o /tmp/$$name 2>/dev/null || { printf "  $(RED)FAIL$(RESET) %s (link)\n" "$$name"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "  $(GREEN)ok$(RESET)   %s (%d)\n" "$$name" "$$got"; ok=$$((ok+1)); \
		else printf "  $(RED)FAIL$(RESET) %s (want %d got %d) — log: tmp/$$name.log\n" "$$name" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)=== $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total ===$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# ─── 依赖文件包含（-MD 自动生成的 .d 实现增量头文件跟踪） ──

-include $(ALL_OBJS:.o=.d)

# ─── 允许 make test 01 05 等带参数的目标 ─────────────────────

%:
	@:

# ─── 自举测试（selfhost）：tcc 独自编译，不依赖 tcc_rt.o / tcc_rt_start.o ──

SELFTESTDIR := compiler-tests/selfhost

test-selfhost: $(BUILD)/tcc
	@ok=0; fail=0; total=0; \
	printf "$(BLUE)=== selfhost tests (no runtime deps) ===$(RESET)\n\n"; \
	for f in $(SELFTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>/dev/null || { printf "  $(RED)FAIL$(RESET) %s (compile)\n" "$$name"; fail=$$((fail+1)); continue; }; \
		$(LD) $(LDTESTFLAGS) /tmp/$$name.o -o /tmp/$$name 2>/dev/null || { printf "  $(RED)FAIL$(RESET) %s (link)\n" "$$name"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "  $(GREEN)ok$(RESET)   %s (%d)\n" "$$name" "$$got"; ok=$$((ok+1)); \
		else printf "  $(RED)FAIL$(RESET) %s (want %d got %d) — log: tmp/$$name.log\n" "$$name" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)=== $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total ===$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]
