#
# Makefile — tcc (ToyCCompiler) 构建系统
#
# 用法：
#   make                        自举构建（bootstrap/tcc + bootstrap/tas）
#   make test                   运行常规测试（29 个）
#   make test 03                指定编号测试
#   make test 03 07             多编号测试
#   make test-selfhost          自包含测试（38 个）
#   make test-source            源文件独立测试（8 个）
#   make test-error             错误报告测试（16 个）
#   make update-bootstrap       用 build 产物更新 bootstrap/ 种子
#   make clean
#
# 全链自举构建。种子二进制见 bootstrap/README.md
#

# ─── 工具链 ────────────────────────────────────────────────────

BOOTSTRAP := bootstrap
CC        := $(BOOTSTRAP)/tcc
AS        := $(BOOTSTRAP)/tas
LD        := $(BOOTSTRAP)/tld

# ─── 标志 ───────────────────────────────────────────────────────

CFLAGS  := -nostdlib -ffreestanding -Wall -Wextra -I include -I compiler
# tcc 忽略所有不识别的 flag（-nostdlib -Wall -Wextra -I -MD 等均无害）。
# tld 直接链接，不需要 LDFLAGS

# ─── 路径 ───────────────────────────────────────────────────────

BUILD   := build
SRC     := compiler
INC     := include

# ─── 头文件依赖（修改后触发增量编译） ─────────────────────────

TCC_NEED := $(INC)/tcc_need.h
ELF_H    := $(INC)/elf.h
ELF_W_H  := $(SRC)/elf_write.h
HEADERS  := $(TCC_NEED) $(ELF_H) $(ELF_W_H)

# ─── 默认目标 ──────────────────────────────────────────────────

.PHONY: all clean update-bootstrap test test-selfhost test-source test-all \
        test-tar

all: $(BUILD)/tcc $(BUILD)/tas $(BUILD)/tld $(BUILD)/tar
	@printf "$(GREEN)✓ 构建完成$(RESET)\n"

# ─── 源文件分组 ────────────────────────────────────────────────

# tcc 编译器
TCC_C_SRCS := tcc.c lex.c parse.c preproc.c cgen.c cgen_expr.c \
              cgen_asm.c cgen_float_hack.c elf_write.c tcc_rt.c
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

# tld 链接器（现已可由 tcc 编译：Symbol.name 改为 const char* 避开了 char 拷贝 bug）
TLD_CFLAGS = -nostdlib -ffreestanding -Wall -Wextra -I include -I compiler
TLD_C_SRCS := tld.c tcc_rt.c
TLD_C_OBJS := $(addprefix $(BUILD)/, $(TLD_C_SRCS:.c=.o))
TLD_OBJS   := $(TLD_C_OBJS) $(BUILD)/tcc_rt_start.o

# tar 归档器
TAR_C_SRCS := tar.c tcc_rt.c
TAR_C_OBJS := $(addprefix $(BUILD)/, $(TAR_C_SRCS:.c=.o))
TAR_OBJS   := $(TAR_C_OBJS) $(BUILD)/tcc_rt_start.o

ALL_OBJS := $(sort $(TCC_OBJS) $(TAS_OBJS) $(TPP_OBJS) $(TLD_OBJS) $(TAR_OBJS))

# ─── 目录创建 ──────────────────────────────────────────────────

$(BUILD):
	mkdir -p $(BUILD)

# ─── 汇编入口 ──────────────────────────────────────────────────

$(BUILD)/tcc_rt_start.o: $(SRC)/tcc_rt_start.S | $(BUILD)
	@printf "  $(BLUE)  AS$(RESET)  %s\n" "$<"
	$(AS) $< -o $@

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

$(BUILD)/tcc: $(BUILD)/tld $(TCC_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tcc ... "
	$(LD) $(TCC_OBJS) -o $@
	@printf "ok\n"
	@printf "$(BLUE)  CC$(RESET)  tcc_rt.c (self) ... "
	@build/tcc -nostdlib -ffreestanding -Wall -Wextra -I include -I compiler -MD -c compiler/tcc_rt.c -o $(BUILD)/tcc_rt_self.o 2>/dev/null; \
	 if [ -f $(BUILD)/tcc_rt_self.o ]; then \
	   printf "$(GREEN)ok$(RESET)\n"; \
	   $(LD) $(filter-out $(BUILD)/tcc_rt.o,$(TCC_OBJS)) $(BUILD)/tcc_rt_self.o -o $@; \
	   mv -f $(BUILD)/tcc_rt_self.o $(BUILD)/tcc_rt.o; \
	 else printf "$(YELLOW)skip$(RESET)\n"; fi
	@size=$$(stat -c%s $@); printf "$(GREEN)ok$(RESET) ($$size bytes)\n"

$(BUILD)/tas: $(BUILD)/tld $(TAS_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tas ... "
	$(LD) $(TAS_OBJS) -o $@
	@printf "$(GREEN)ok$(RESET)\n"

$(BUILD)/tpp: $(BUILD)/tld $(TPP_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tpp ... "
	$(LD) $(TPP_OBJS) -o $@
	@printf "$(GREEN)ok$(RESET)\n"

$(BUILD)/tar: $(BUILD)/tld $(TAR_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tar ... "
	$(LD) $(TAR_OBJS) -o $@
	@size=$$(stat -c%s $@); printf "$(GREEN)ok$(RESET) ($$size bytes)\n"

$(BUILD)/tld: $(TLD_OBJS)
	@printf "$(BLUE)  LD$(RESET)  tld ... "
	$(LD) $(TLD_OBJS) -o $@
	@size=$$(stat -c%s $@); printf "$(GREEN)ok$(RESET) ($$size bytes)\n"

# ─── 清理 ──────────────────────────────────────────────────────

clean:
	@printf "$(BLUE)  CLEAN$(RESET) 删除构建产物 ... "
	rm -rf $(BUILD) tmp
	@printf "$(GREEN)done$(RESET)\n"

# ─── 更新自举种子 ──────────────────────────────────────────────

update-bootstrap: $(BUILD)/tcc $(BUILD)/tas $(BUILD)/tld $(BUILD)/tar
	@printf "$(BLUE)  BOOTSTRAP$(RESET) 更新自举种子 ...\n"
	@mkdir -p $(BOOTSTRAP)
	cp $(BUILD)/tcc $(BOOTSTRAP)/tcc
	cp $(BUILD)/tas $(BOOTSTRAP)/tas
	cp $(BUILD)/tld $(BOOTSTRAP)/tld
	cp $(BUILD)/tar $(BOOTSTRAP)/tar
	@printf "$(GREEN)✓ 种子已更新: bootstrap/{tcc,tas,tld,tar}$(RESET)\n"

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
# 常规测试
test: $(BUILD)/tcc $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o $(BUILD)/tld
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ tcc 常规测试 ══════$(RESET)\n"; \
	printf "\n"; \
	for f in $(TESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  tcc → $(BLUE)%-26s$(RESET) " "$$name"; \
		$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>/dev/null || { printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(BUILD)/tld /tmp/$$name.o $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o -o /tmp/$$name 2>/dev/null || { printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else printf "$(RED)✗$(RESET) (want %d got %d) — tmp/$$name.log\n" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# 自包含测试（无 tcc_rt 依赖）
test-selfhost: $(BUILD)/tcc $(BUILD)/tld
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
		$(BUILD)/tld /tmp/$$name.o -o /tmp/$$name 2>/dev/null || { printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else printf "$(RED)✗$(RESET) (want %d got %d) — tmp/$$name.log\n" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# ─── 源文件独立测试 ───────────────────────────────────────────

SOURCETESTDIR := compiler-tests/source
test-source: $(BUILD)/tcc $(BUILD)/tld
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ source 测试（tcc 编译）══════$(RESET)\n\n"; \
	for f in $(SOURCETESTDIR)/*.c; do \
		name=$$(basename "$$f" .c); \
		total=$$((total+1)); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  $(BLUE)%-25s$(RESET) " "$$name"; \
		$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>tmp/test_source_$$name-compile.log; \
		if [ $$? -ne 0 ]; then \
			printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; \
		fi; \
		$(BUILD)/tld /tmp/$$name.o -o /tmp/test_$$name 2>tmp/test_source_$$name-link.log; \
		if [ $$? -ne 0 ]; then \
			printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; \
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

# ─── 全部测试 ──────────────────────────────────────────────────

test-all: test test-selfhost test-source test-lib test-tld test-error test-tar test-tld-archive
	@printf "$(GREEN)✓ 全部测试通过$(RESET)\n"

# ─── Tinylibc 库测试（从真实 Tinylibc 源文件编译） ─────────────

TINYLIBC_DIR   := ../Tinylibc
LIBT_OBJDIR    := /tmp/libt_obj

# -Icompiler-tests/lib/override 保留空目录以备后续遮蔽需要
TINYLIBC_CFLAGS := -Icompiler-tests/lib/override \
                   -I$(TINYLIBC_DIR)/include \
                   -I$(TINYLIBC_DIR)/include/posix \
                   -I$(TINYLIBC_DIR)/include/tlibc \
                   -I$(TINYLIBC_DIR)/arch \
                   -I$(TINYLIBC_DIR)/arch/x86_64 \
                   -DX86_64_TLIBC=1

# 引入 lib 模块声明（LIBS, _SRCS_*, _DEPS_*, _TEST_*）
include compiler-tests/lib/libs.mk

# 路径转换：math/math.c -> /tmp/libt_obj/math_math.o
_lib_obj = $(LIBT_OBJDIR)/$(subst /,_,$(1:.c=.o))
# 单个 lib 的所有目标文件
_lib_objs = $(foreach src,$(_SRCS_$(1)),$(call _lib_obj,$(src)))
# lib + 直接依赖的所有目标文件（排序去重）
_lib_bundle = $(sort $(call _lib_objs,$(1)) \
               $(foreach dep,$(_DEPS_$(1)),$(call _lib_objs,$(dep))))

.PHONY: test-lib-compile

test-lib-compile: $(BUILD)/tcc
	@mkdir -p $(LIBT_OBJDIR) tmp; \
	ok=0; fail=0; total=0; \
	printf "$(BLUE)══════ Tinylibc 库编译检查 ══════$(RESET)\n\n"; \
	$(foreach lib,$(LIBS), \
	  $(foreach src,$(_SRCS_$(lib)), \
	    printf "  $(BLUE)%-25s$(RESET) " "$(src)"; \
	    $(BUILD)/tcc $(TINYLIBC_CFLAGS) -c $(TINYLIBC_DIR)/lib/$(src) \
	      -o $(call _lib_obj,$(src)) 2>/tmp/libt_$(lib).log \
	    && { printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); } \
	    || { printf "$(RED)✗$(RESET)\n"; cat /tmp/libt_$(lib).log; fail=$$((fail+1)); }; \
	    total=$$((total+1)); \
	  ) \
	) \
	printf "\n$(BLUE)══════$(RESET) 编译: $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

.PHONY: test-lib

test-lib: $(BUILD)/tcc $(BUILD)/tld $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o
	@mkdir -p $(LIBT_OBJDIR) tmp; \
	\
	# Phase 1: 编译所有源文件 \
	ok=0; fail=0; total=0; \
	printf "$(BLUE)══ Tinylibc 库编译测试 ══$(RESET)\n\n"; \
	$(foreach lib,$(LIBS), \
	  $(foreach src,$(_SRCS_$(lib)), \
	    printf "  $(BLUE)%-25s$(RESET) " "$(src)"; \
	    $(BUILD)/tcc $(TINYLIBC_CFLAGS) -c $(TINYLIBC_DIR)/lib/$(src) \
	      -o $(call _lib_obj,$(src)) 2>/tmp/libt_$(lib).log \
	    && { printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); } \
	    || { printf "$(RED)✗$(RESET)\n"; cat /tmp/libt_$(lib).log; fail=$$((fail+1)); }; \
	    total=$$((total+1)); \
	  ) \
	) \
	printf "\n$(BLUE)══$(RESET) 编译: $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	if [ "$$fail" -ne 0 ]; then exit 1; fi; \
	\
	# Phase 2: 功能测试 \
	printf "\n$(BLUE)══ 功能测试 ══$(RESET)\n\n"; \
	ft_ok=0; ft_fail=0; ft_total=0; \
	$(foreach lib,$(LIBS), \
	  $(if $(_TEST_$(lib)), \
	    printf "  $(BLUE)%-25s$(RESET) " "$(lib)"; \
	    $(BUILD)/tcc $(TINYLIBC_CFLAGS) -c compiler-tests/lib/$(_TEST_$(lib)).c \
	      -o $(LIBT_OBJDIR)/tdrv_$(lib).o 2>/tmp/libt_$(lib)_tdrv.log \
	    && { \
	      $(BUILD)/tld $(call _lib_bundle,$(lib)) $(LIBT_OBJDIR)/tdrv_$(lib).o \
	        $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o \
	        -o /tmp/libt_$(lib) 2>/tmp/libt_$(lib)_link.log \
	      && { \
	        /tmp/libt_$(lib) > tmp/libt_$(lib).log 2>&1; got=$$?; \
	        expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' \
	          compiler-tests/lib/$(_TEST_$(lib)).c | head -1); \
	        [ -z "$$expect" ] && expect=0; \
	        if [ "$$got" = "$$expect" ]; then \
	          printf "$(GREEN)✓$(RESET) (exit %d)\n" "$$got"; ft_ok=$$((ft_ok+1)); \
	        else \
	          printf "$(RED)✗$(RESET) (want %d got %d)\n" "$$expect" "$$got"; \
	          cat tmp/libt_$(lib).log; ft_fail=$$((ft_fail+1)); \
	        fi; \
	      } || { \
	        printf "$(RED)LINK FAIL$(RESET)\n"; cat /tmp/libt_$(lib)_link.log; \
	        ft_fail=$$((ft_fail+1)); \
	      }; \
	    } || { \
	      printf "$(RED)COMPILE FAIL$(RESET)\n"; cat /tmp/libt_$(lib)_tdrv.log; \
	      ft_fail=$$((ft_fail+1)); \
	    }; \
	    ft_total=$$((ft_total+1)); \
	  ) \
	) \
	if [ "$$ft_total" -gt 0 ]; then \
	  printf "\n$(BLUE)══$(RESET) 功能: $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══$(RESET)\n" \
	    "$$ft_ok" "$$ft_fail" "$$ft_total"; \
	fi; \
	[ "$$fail" -eq 0 ] && [ "$$ft_fail" -eq 0 ]

# ─── tld 链接器测试 ───────────────────────────────────────────

TLD_TESTDIR := compiler-tests/tld

# tld 自包含测试：用 tld 替代 ld 运行 selfhost 测试
test-tld: $(BUILD)/tld $(BUILD)/tcc
	@printf "$(BLUE)══════ tld 自包含测试（selfhost 测试 × tld 链接）══════$(RESET)\n\n"; \
	ok=0; fail=0; total=0; mkdir -p tmp; \
	for f in $(SELFTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  tcc+tld → $(BLUE)%-24s$(RESET) " "$$name"; \
		$(BUILD)/tcc "$$f" -o /tmp/tld_$$name.o 2>/tmp/tld_$$name-compile.log || { \
			printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(BUILD)/tld /tmp/tld_$$name.o -o /tmp/tld_$$name 2>/tmp/tld_$$name-link.log || { \
			printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/tld_$$name >/tmp/tld_$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then \
			printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else \
			printf "$(RED)✗$(RESET) (want %d got %d) — /tmp/tld_$$name.log\n" "$$expect" "$$got"; \
			fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# tld 多文件链接测试
test-tld-multifile: $(BUILD)/tld $(BUILD)/tcc
	@printf "$(BLUE)══════ tld 多文件链接测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; \
	printf "  mf_helper + mf_main ... "; \
	if $(BUILD)/tcc -nostdlib -ffreestanding -I include -I compiler \
		$(TLD_TESTDIR)/mf_helper.c -o /tmp/tld_mf_helper.o 2>/tmp/tld_mf_compile.log && \
	   $(BUILD)/tcc -nostdlib -ffreestanding -I include -I compiler \
		$(TLD_TESTDIR)/mf_main.c -o /tmp/tld_mf_main.o 2>>/tmp/tld_mf_compile.log; then \
		:; \
	else \
		printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=1; \
	fi; \
	if [ $$fail -eq 0 ]; then \
		if $(BUILD)/tld /tmp/tld_mf_main.o /tmp/tld_mf_helper.o \
			-o /tmp/tld_mf_test 2>/tmp/tld_mf_link.log; then \
			/tmp/tld_mf_test >/tmp/tld_mf_test.log 2>&1; got=$$?; \
			if [ "$$got" = "0" ]; then \
				printf "$(GREEN)✓$(RESET) (exit 0)\n"; ok=1; \
			else \
				printf "$(RED)✗$(RESET) (exit $$got) — /tmp/tld_mf_test.log\n"; fail=1; \
			fi; \
		else \
			printf "$(RED)LINK FAIL$(RESET)\n"; cat /tmp/tld_mf_link.log; fail=1; \
		fi; \
	fi; \
	\
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), 1 total $(BLUE)══════$(RESET)\n\n" "$$ok" "$$fail"; \
	[ "$$fail" -eq 0 ]

# tld 自测试：tld 链接自身 + 运行自我验证
test-tld-self: $(BUILD)/tld $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o
	@printf "$(BLUE)══════ tld 自链接测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; \
	\
	# 1) 用 build/tld 链接自身 .o 文件 \
	printf "  1) 自链接 stage-1 ... "; \
	if $(BUILD)/tld $(BUILD)/tld.o $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o \
		-o $(BUILD)/tld-stage1 2>/tmp/tld-stage1.log; then \
		printf "$(GREEN)✓$(RESET)\n"; ok=1; \
	else \
		printf "$(RED)✗$(RESET) (see /tmp/tld-stage1.log)\n"; fail=1; \
	fi; \
	\
	if [ "$$ok" -eq 1 ]; then \
		# 2) stage-1 再链接自身 → stage-2 \
		printf "  2) 自链接 stage-2 ... "; \
		if $(BUILD)/tld-stage1 $(BUILD)/tld.o $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o \
			-o $(BUILD)/tld-stage2 2>/tmp/tld-stage2.log; then \
			printf "$(GREEN)✓$(RESET)\n"; \
			# 3) 检查 stage-1 和 stage-2 字节级一致 \
			if diff $(BUILD)/tld-stage1 $(BUILD)/tld-stage2 >/dev/null 2>&1; then \
				printf "  3) 收敛检查: $(GREEN)字节级一致$(RESET) ✅\n"; \
			else \
				printf "  3) 收敛检查: $(RED)字节不同$(RESET)\n"; fail=1; \
			fi; \
		else \
			printf "$(RED)✗$(RESET) (see /tmp/tld-stage2.log)\n"; fail=1; \
		fi; \
	fi; \
	\
	if [ "$$fail" -eq 0 ]; then \
		printf "\n$(GREEN)✓ tld 自举验证通过$(RESET)\n"; \
	else \
		printf "\n$(RED)✗ tld 自举验证失败$(RESET)\n"; \
	fi

# ─── 错误报告测试 ───────────────────────────────────────────

ERRTESTDIR := compiler-tests/error
test-error: $(BUILD)/tcc
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ tcc 错误报告测试 ══════$(RESET)\n\n"; \
	for f in $(ERRTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect_err=$$(sed -n 's/.*EXPECT_ERR:[[:space:]]*\(.*\)\*\//\1/p' "$$f" | head -1 | sed 's/[[:space:]]*$$//'); \
		printf "  $(BLUE)━━━ %s ━━━$(RESET)\n" "$$name"; \
		output=$$($(BUILD)/tcc "$$f" -o /tmp/err_$$name.o 2>&1); \
		rc=$$?; \
		logfile="tmp/err_$${name}.log"; \
		echo "$$output" > "$$logfile"; \
		printf "  tcc output:\n"; \
		if [ -n "$$output" ]; then \
			echo "$$output" | sed 's/^/    /'; \
		else \
			echo "    (no output)"; \
		fi; \
		if [ $$rc -eq 0 ]; then \
			printf "  $(RED)✗ 编译成功，但期望报错$(RESET)\n"; \
			fail=$$((fail+1)); \
		elif [ -n "$$expect_err" ] && ! echo "$$output" | grep -qF "$$expect_err"; then \
			printf "  $(RED)✗ 错误模式不匹配$(RESET)\n"; \
			printf "    expect: %s\n" "$$expect_err"; \
			fail=$$((fail+1)); \
		else \
			printf "  $(GREEN)✓ 错误正确$(RESET)\n"; \
			ok=$$((ok+1)); \
		fi; \
		printf "\n"; \
	done; \
	printf "$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# ─── tld 归档链接测试 ───────────────────────────────────────

test-tld-archive: $(BUILD)/tld $(BUILD)/tcc $(BUILD)/tar $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o
	@printf "$(BLUE)══════ tld 归档链接测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; total=0; \
	\
	printf "  $(BLUE)%-25s$(RESET) " "archive_basic"; \
	if $(BUILD)/tcc -nostdlib -ffreestanding -I include -I compiler \
		$(TLD_TESTDIR)/arch_helper.c -o /tmp/arch_helper.o 2>/tmp/arch_compile.log \
		&& $(BUILD)/tcc -nostdlib -ffreestanding -I include -I compiler \
		   $(TLD_TESTDIR)/arch_main.c -o /tmp/arch_main.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/tar rcs /tmp/libarch_basic.a /tmp/arch_helper.o 2>/dev/null \
		&& $(BUILD)/tld /tmp/arch_main.o /tmp/libarch_basic.a \
		   $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o -o /tmp/arch_basic_test 2>/tmp/arch_link.log; then \
		/tmp/arch_basic_test >/tmp/arch_basic_test.log 2>&1; got=$$?; \
		if [ "$$got" = "42" ]; then \
			printf "$(GREEN)✓$(RESET) (exit $$got)\n"; ok=$$((ok+1)); \
		else \
			printf "$(RED)✗$(RESET) (want 42 got $$got)\n"; fail=$$((fail+1)); \
		fi; \
	else \
		printf "$(RED)✗$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	printf "  $(BLUE)%-25s$(RESET) " "archive_transitive"; \
	if $(BUILD)/tcc -nostdlib -ffreestanding -I include -I compiler \
		$(TLD_TESTDIR)/arch_leaf.c -o /tmp/arch_leaf.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/tcc -nostdlib -ffreestanding -I include -I compiler \
		   $(TLD_TESTDIR)/arch_mid.c -o /tmp/arch_mid.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/tcc -nostdlib -ffreestanding -I include -I compiler \
		   $(TLD_TESTDIR)/arch_trans.c -o /tmp/arch_trans.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/tar rcs /tmp/libarch_trans.a /tmp/arch_mid.o /tmp/arch_leaf.o 2>/dev/null \
		&& $(BUILD)/tld /tmp/arch_trans.o /tmp/libarch_trans.a \
		   $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o -o /tmp/arch_trans_test 2>/tmp/arch_link.log; then \
		/tmp/arch_trans_test >/tmp/arch_trans_test.log 2>&1; got=$$?; \
		if [ "$$got" = "42" ]; then \
			printf "$(GREEN)✓$(RESET) (exit $$got)\n"; ok=$$((ok+1)); \
		else \
			printf "$(RED)✗$(RESET) (want 42 got $$got)\n"; fail=$$((fail+1)); \
		fi; \
	else \
		printf "$(RED)✗$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	# 清理临时文件 \
	rm -f /tmp/arch_helper.o /tmp/arch_main.o /tmp/arch_leaf.o /tmp/arch_mid.o /tmp/arch_trans.o; \
	rm -f /tmp/libarch_basic.a /tmp/libarch_trans.a; \
	rm -f /tmp/arch_basic_test /tmp/arch_trans_test; \
	rm -f /tmp/arch_compile.log /tmp/arch_link.log; \
	rm -f /tmp/arch_basic_test.log /tmp/arch_trans_test.log; \
	\
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# ─── tar 归档器测试 ────────────────────────────────────────────

TARTESTDIR := compiler-tests/tar
test-tar: $(BUILD)/tar $(BUILD)/tcc $(BUILD)/tld $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o
	@printf "$(BLUE)══════ tar 归档器测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; total=0; mkdir -p tmp; \
	\
	for f in $(TARTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  $(BLUE)%-25s$(RESET) " "$$name"; \
		$(BUILD)/tcc "$$f" -o /tmp/$$name.o 2>/tmp/tar_$$name-compile.log || { \
			printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(BUILD)/tld /tmp/$$name.o $(BUILD)/tcc_rt.o $(BUILD)/tcc_rt_start.o \
			-o /tmp/tar_$$name 2>/tmp/tar_$$name-link.log || { \
			printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/tar_$$name >tmp/tar_$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then \
			printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else \
			printf "$(RED)✗$(RESET) (want %d got %d) — tmp/tar_$$name.log\n" "$$expect" "$$got"; \
			fail=$$((fail+1)); \
		fi; \
	done; \
	\
	# 归档创建+列表+提取+符号表测试 \
	printf "  $(BLUE)%-25s$(RESET) " "archive_create"; \
	echo 'int fn(void){return 42;}' > /tmp/tar_test_fn.c && \
	if $(BUILD)/tcc /tmp/tar_test_fn.c -o /tmp/tar_test_fn.o 2>/dev/null; then \
		if $(BUILD)/tar rcs /tmp/tar_test.a /tmp/tar_test_fn.o 2>/dev/null; then \
			if [ -f /tmp/tar_test.a ]; then \
				printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
			else \
				printf "$(RED)✗ (archive not created)$(RESET)\n"; fail=$$((fail+1)); \
			fi; \
		else \
			printf "$(RED)✗ (tar failed)$(RESET)\n"; fail=$$((fail+1)); \
		fi; \
	else \
		printf "$(RED)✗ (compile failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	printf "  $(BLUE)%-25s$(RESET) " "archive_list"; \
	if $(BUILD)/tar t /tmp/tar_test.a 2>/dev/null | grep -q tar_test_fn 2>/dev/null; then \
		printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
	else \
		printf "$(RED)✗ (list failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	printf "  $(BLUE)%-25s$(RESET) " "archive_extract"; \
	rm -f tar_test_fn.o && \
	if $(BUILD)/tar x /tmp/tar_test.a 2>/dev/null && [ -f tar_test_fn.o ]; then \
		rm -f tar_test_fn.o; \
		printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
	else \
		printf "$(RED)✗ (extract failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	printf "  $(BLUE)%-25s$(RESET) " "symtab_test"; \
	echo 'int global_func(void){return 99;}' > /tmp/tar_sym.c && \
	if $(BUILD)/tcc /tmp/tar_sym.c -o /tmp/tar_sym.o 2>/dev/null \
	   && $(BUILD)/tar rcs /tmp/tar_sym.a /tmp/tar_sym.o 2>/dev/null; then \
		printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
	else \
		printf "$(RED)✗ (symtab test failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	# 清理 \
	rm -f /tmp/tar_test_fn.c /tmp/tar_test_fn.o /tmp/tar_test.a; \
	rm -f /tmp/tar_sym.c /tmp/tar_sym.o /tmp/tar_sym.a; \
	\
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]
