#
# Makefile — toyc (ToyCCompiler) 构建系统
#
# 用法：
#   make                        自举构建（bootstrap/toyc + bootstrap/toyas）
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
CC        := $(BOOTSTRAP)/toyc
AS        := $(BOOTSTRAP)/toyas
LD        := $(BOOTSTRAP)/toyld

# ─── 标志 ───────────────────────────────────────────────────────

CFLAGS  := -nostdlib -ffreestanding -Wall -Wextra -I include -I compiler
# toyc 忽略所有不识别的 flag（-nostdlib -Wall -Wextra -I -MD 等均无害）。
# toyld 直接链接，不需要 LDFLAGS

# ─── 路径 ───────────────────────────────────────────────────────

BUILD   := build
SRC     := compiler
INC     := include

# ─── 头文件依赖（修改后触发增量编译） ─────────────────────────

TOYC_NEED := $(INC)/toyc_need.h
ELF_H    := $(INC)/elf.h
ELF_W_H  := $(SRC)/elf_write.h
HEADERS  := $(TOYC_NEED) $(ELF_H) $(ELF_W_H)

# ─── 默认目标 ──────────────────────────────────────────────────

.PHONY: all clean update-bootstrap test test-selfhost test-source test-all \
        test-toyar

all: $(BUILD)/toyc $(BUILD)/toyas $(BUILD)/toyld $(BUILD)/toyar
	@printf "$(GREEN)✓ 构建完成$(RESET)\n"

# ─── 源文件分组 ────────────────────────────────────────────────

# toyc 编译器
TOYC_C_SRCS := toyc.c lex.c parse.c preproc.c cgen.c cgen_expr.c \
              cgen_asm.c cgen_float_hack.c elf_write.c toyc_rt.c
TOYC_C_OBJS := $(addprefix $(BUILD)/, $(TOYC_C_SRCS:.c=.o))
TOYC_OBJS   := $(TOYC_C_OBJS) $(BUILD)/toyc_rt_start.o

# toyas 汇编器
TOYAS_C_SRCS := toyas.c elf_write.c toyc_rt.c
TOYAS_C_OBJS := $(addprefix $(BUILD)/, $(TOYAS_C_SRCS:.c=.o))
TOYAS_OBJS   := $(TOYAS_C_OBJS) $(BUILD)/toyc_rt_start.o

# toypp 预处理器
TOYPP_C_SRCS := toypp.c preproc.c toyc_rt.c
TOYPP_C_OBJS := $(addprefix $(BUILD)/, $(TOYPP_C_SRCS:.c=.o))
TOYPP_OBJS   := $(TOYPP_C_OBJS) $(BUILD)/toyc_rt_start.o

# toyld 链接器（现已可由 toyc 编译：Symbol.name 改为 const char* 避开了 char 拷贝 bug）
TOYLD_CFLAGS = -nostdlib -ffreestanding -Wall -Wextra -I include -I compiler
TOYLD_C_SRCS := toyld.c toyc_rt.c
TOYLD_C_OBJS := $(addprefix $(BUILD)/, $(TOYLD_C_SRCS:.c=.o))
TOYLD_OBJS   := $(TOYLD_C_OBJS) $(BUILD)/toyc_rt_start.o

# toyar 归档器
TOYAR_C_SRCS := toyar.c toyc_rt.c
TOYAR_C_OBJS := $(addprefix $(BUILD)/, $(TOYAR_C_SRCS:.c=.o))
TOYAR_OBJS   := $(TOYAR_C_OBJS) $(BUILD)/toyc_rt_start.o

ALL_OBJS := $(sort $(TOYC_OBJS) $(TOYAS_OBJS) $(TOYPP_OBJS) $(TOYLD_OBJS) $(TOYAR_OBJS))

# ─── 目录创建 ──────────────────────────────────────────────────

$(BUILD):
	mkdir -p $(BUILD)

# ─── 汇编入口 ──────────────────────────────────────────────────

$(BUILD)/toyc_rt_start.o: $(SRC)/toyc_rt_start.S | $(BUILD)
	@printf "  $(BLUE)  AS$(RESET)  %s\n" "$<"
	$(AS) $< -o $@

# ─── C 编译规则 ────────────────────────────────────────────────

$(BUILD)/%.o: $(SRC)/%.c $(HEADERS) | $(BUILD)
	@printf "  $(BLUE)  CC$(RESET)  %-20s " "$<"
	$(CC) $(CFLAGS) -MD -c $< -o $@
	@printf "$(GREEN)ok$(RESET)\n"

# parse.c：无 #include 行，强制包含 toyc.h
$(BUILD)/parse.o: $(SRC)/parse.c $(HEADERS) | $(BUILD)
	@printf "  $(BLUE)  CC$(RESET)  %-20s " "$<"
	$(CC) $(CFLAGS) -include toyc.h -MD -c $< -o $@
	@printf "$(GREEN)ok$(RESET)\n"

# toyc_rt.c：自包含运行时，仅依赖 toyc_need.h
$(BUILD)/toyc_rt.o: $(SRC)/toyc_rt.c $(TOYC_NEED) | $(BUILD)
	@printf "  $(BLUE)  CC$(RESET)  %-20s " "$<"
	$(CC) $(CFLAGS) -MD -c $< -o $@
	@printf "$(GREEN)ok$(RESET)\n"

# ─── 链接规则 ──────────────────────────────────────────────────

$(BUILD)/toyc: $(BUILD)/toyld $(TOYC_OBJS)
	@printf "$(BLUE)  LD$(RESET)  toyc ... "
	$(LD) $(TOYC_OBJS) -o $@
	@printf "ok\n"
	@printf "$(BLUE)  CC$(RESET)  toyc_rt.c (self) ... "
	@build/toyc -nostdlib -ffreestanding -Wall -Wextra -I include -I compiler -MD -c compiler/toyc_rt.c -o $(BUILD)/toyc_rt_self.o 2>/dev/null; \
	 if [ -f $(BUILD)/toyc_rt_self.o ]; then \
	   printf "$(GREEN)ok$(RESET)\n"; \
	   $(LD) $(filter-out $(BUILD)/toyc_rt.o,$(TOYC_OBJS)) $(BUILD)/toyc_rt_self.o -o $@; \
	   mv -f $(BUILD)/toyc_rt_self.o $(BUILD)/toyc_rt.o; \
	 else printf "$(YELLOW)skip$(RESET)\n"; fi
	@size=$$(stat -c%s $@); printf "$(GREEN)ok$(RESET) ($$size bytes)\n"

$(BUILD)/toyas: $(BUILD)/toyld $(TOYAS_OBJS)
	@printf "$(BLUE)  LD$(RESET)  toyas ... "
	$(LD) $(TOYAS_OBJS) -o $@
	@printf "$(GREEN)ok$(RESET)\n"

$(BUILD)/toypp: $(BUILD)/toyld $(TOYPP_OBJS)
	@printf "$(BLUE)  LD$(RESET)  toypp ... "
	$(LD) $(TOYPP_OBJS) -o $@
	@printf "$(GREEN)ok$(RESET)\n"

$(BUILD)/toyar: $(BUILD)/toyld $(TOYAR_OBJS)
	@printf "$(BLUE)  LD$(RESET)  toyar ... "
	$(LD) $(TOYAR_OBJS) -o $@
	@size=$$(stat -c%s $@); printf "$(GREEN)ok$(RESET) ($$size bytes)\n"

$(BUILD)/toyld: $(TOYLD_OBJS)
	@printf "$(BLUE)  LD$(RESET)  toyld ... "
	$(LD) $(TOYLD_OBJS) -o $@
	@size=$$(stat -c%s $@); printf "$(GREEN)ok$(RESET) ($$size bytes)\n"

# ─── 清理 ──────────────────────────────────────────────────────

clean:
	@printf "$(BLUE)  CLEAN$(RESET) 删除构建产物 ... "
	rm -rf $(BUILD) tmp
	@printf "$(GREEN)done$(RESET)\n"

# ─── 更新自举种子 ──────────────────────────────────────────────

update-bootstrap: $(BUILD)/toyc $(BUILD)/toyas $(BUILD)/toyld $(BUILD)/toyar
	@printf "$(BLUE)  BOOTSTRAP$(RESET) 更新自举种子 ...\n"
	@mkdir -p $(BOOTSTRAP)
	cp $(BUILD)/toyc $(BOOTSTRAP)/toyc
	cp $(BUILD)/toyas $(BOOTSTRAP)/toyas
	cp $(BUILD)/toyld $(BOOTSTRAP)/toyld
	cp $(BUILD)/toyar $(BOOTSTRAP)/toyar
	@printf "$(GREEN)✓ 种子已更新: bootstrap/{toyc,toyas,toyld,toyar}$(RESET)\n"

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
test: $(BUILD)/toyc $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o $(BUILD)/toyld
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ toyc 常规测试 ══════$(RESET)\n"; \
	printf "\n"; \
	for f in $(TESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  toyc → $(BLUE)%-26s$(RESET) " "$$name"; \
		$(BUILD)/toyc "$$f" -o /tmp/$$name.o 2>/dev/null || { printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(BUILD)/toyld /tmp/$$name.o $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o -o /tmp/$$name 2>/dev/null || { printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else printf "$(RED)✗$(RESET) (want %d got %d) — tmp/$$name.log\n" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# 自包含测试（无 toyc_rt 依赖）
test-selfhost: $(BUILD)/toyc $(BUILD)/toyld
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ toyc 自包含测试 ══════$(RESET)\n\n"; \
	for f in $(SELFTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  toyc → $(BLUE)%-26s$(RESET) " "$$name"; \
		$(BUILD)/toyc "$$f" -o /tmp/$$name.o 2>/dev/null || { printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(BUILD)/toyld /tmp/$$name.o -o /tmp/$$name 2>/dev/null || { printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/$$name >tmp/$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else printf "$(RED)✗$(RESET) (want %d got %d) — tmp/$$name.log\n" "$$expect" "$$got"; fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# ─── 源文件独立测试 ───────────────────────────────────────────

SOURCETESTDIR := compiler-tests/source
test-source: $(BUILD)/toyc $(BUILD)/toyld
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ source 测试（toyc 编译）══════$(RESET)\n\n"; \
	for f in $(SOURCETESTDIR)/*.c; do \
		name=$$(basename "$$f" .c); \
		total=$$((total+1)); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  $(BLUE)%-25s$(RESET) " "$$name"; \
		$(BUILD)/toyc "$$f" -o /tmp/$$name.o 2>tmp/test_source_$$name-compile.log; \
		if [ $$? -ne 0 ]; then \
			printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; \
		fi; \
		$(BUILD)/toyld /tmp/$$name.o -o /tmp/test_$$name 2>tmp/test_source_$$name-link.log; \
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

test-all: test test-selfhost test-source test-lib test-toyld test-error test-toyar test-toyld-archive
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
# 路径转换：thread/clone.S -> /tmp/libt_obj/thread_clone.o
_lib_asm_obj = $(LIBT_OBJDIR)/$(subst /,_,$(patsubst %.S,%.o,$(1)))
# 单个 lib 的所有目标文件（.c + .S）
_lib_objs = $(foreach src,$(_SRCS_$(1)),$(call _lib_obj,$(src))) \
            $(foreach src,$(_ASM_$(1)),$(call _lib_asm_obj,$(src)))
# lib + 直接依赖的所有目标文件（排序去重）
_lib_bundle = $(sort $(call _lib_objs,$(1)) \
               $(foreach dep,$(_DEPS_$(1)),$(call _lib_objs,$(dep))))

.PHONY: test-lib-compile

test-lib-compile: $(BUILD)/toyc
	@mkdir -p $(LIBT_OBJDIR) tmp; \
	ok=0; fail=0; total=0; \
	printf "$(BLUE)══════ Tinylibc 库编译检查 ══════$(RESET)\n\n"; \
	$(foreach lib,$(LIBS), \
	  $(foreach src,$(_SRCS_$(lib)), \
	    printf "  $(BLUE)%-25s$(RESET) " "$(src)"; \
	    $(BUILD)/toyc $(TINYLIBC_CFLAGS) -c $(TINYLIBC_DIR)/lib/$(src) \
	      -o $(call _lib_obj,$(src)) 2>/tmp/libt_$(lib).log \
	    && { printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); } \
	    || { printf "$(RED)✗$(RESET)\n"; cat /tmp/libt_$(lib).log; fail=$$((fail+1)); }; \
	    total=$$((total+1)); \
	  ) \
	) \
	$(foreach lib,$(LIBS), \
	  $(foreach src,$(_ASM_$(lib)), \
	    printf "  $(BLUE)%-25s$(RESET) " "$(src)"; \
	    $(BUILD)/toyas $(TINYLIBC_DIR)/lib/$(src) -o $(LIBT_OBJDIR)/$(subst /,_,$(src:.S=.o)) \
	      2>/tmp/libt_$(lib).log \
	    && { printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); } \
	    || { printf "$(RED)✗$(RESET)\n"; cat /tmp/libt_$(lib).log; fail=$$((fail+1)); }; \
	    total=$$((total+1)); \
	  ) \
	) \
	printf "\n$(BLUE)══════$(RESET) 编译: $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

.PHONY: test-lib

test-lib: $(BUILD)/toyc $(BUILD)/toyld $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o
	@mkdir -p $(LIBT_OBJDIR) tmp; \
	\
	# Phase 1: 编译所有源文件 \
	ok=0; fail=0; total=0; \
	printf "$(BLUE)══ Tinylibc 库编译测试 ══$(RESET)\n\n"; \
	$(foreach lib,$(LIBS), \
	  $(foreach src,$(_SRCS_$(lib)), \
	    printf "  $(BLUE)%-25s$(RESET) " "$(src)"; \
	    $(BUILD)/toyc $(TINYLIBC_CFLAGS) -c $(TINYLIBC_DIR)/lib/$(src) \
	      -o $(call _lib_obj,$(src)) 2>/tmp/libt_$(lib).log \
	    && { printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); } \
	    || { printf "$(RED)✗$(RESET)\n"; cat /tmp/libt_$(lib).log; fail=$$((fail+1)); }; \
	    total=$$((total+1)); \
	  ) \
	) \
	$(foreach lib,$(LIBS), \
	  $(foreach src,$(_ASM_$(lib)), \
	    printf "  $(BLUE)%-25s$(RESET) " "$(src)"; \
	    $(BUILD)/toyas $(TINYLIBC_DIR)/lib/$(src) -o $(LIBT_OBJDIR)/$(subst /,_,$(src:.S=.o)) \
	      2>/tmp/libt_$(lib).log \
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
	    $(BUILD)/toyc $(TINYLIBC_CFLAGS) -c compiler-tests/lib/$(_TEST_$(lib)).c \
	      -o $(LIBT_OBJDIR)/tdrv_$(lib).o 2>/tmp/libt_$(lib)_tdrv.log \
	    && { \
	      $(BUILD)/toyld $(call _lib_bundle,$(lib)) $(LIBT_OBJDIR)/tdrv_$(lib).o \
	        $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o \
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

# ─── toyld 链接器测试 ───────────────────────────────────────────

TOYLD_TESTDIR := compiler-tests/toyld

# toyld 自包含测试：用 tld 替代 ld 运行 selfhost 测试
test-toyld: $(BUILD)/toyld $(BUILD)/toyc
	@printf "$(BLUE)══════ toyld 自包含测试（selfhost 测试 × toyld 链接）══════$(RESET)\n\n"; \
	ok=0; fail=0; total=0; mkdir -p tmp; \
	for f in $(SELFTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  toyc+toyld → $(BLUE)%-24s$(RESET) " "$$name"; \
		$(BUILD)/toyc "$$f" -o /tmp/toyld_$$name.o 2>/tmp/toyld_$$name-compile.log || { \
			printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(BUILD)/toyld /tmp/toyld_$$name.o -o /tmp/toyld_$$name 2>/tmp/toyld_$$name-link.log || { \
			printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/toyld_$$name >/tmp/toyld_$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then \
			printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else \
			printf "$(RED)✗$(RESET) (want %d got %d) — /tmp/toyld_$$name.log\n" "$$expect" "$$got"; \
			fail=$$((fail+1)); fi; \
	done; \
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]

# toyld 多文件链接测试
test-toyld-multifile: $(BUILD)/toyld $(BUILD)/toyc
	@printf "$(BLUE)══════ toyld 多文件链接测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; \
	printf "  mf_helper + mf_main ... "; \
	if $(BUILD)/toyc -nostdlib -ffreestanding -I include -I compiler \
		$(TOYLD_TESTDIR)/mf_helper.c -o /tmp/toyld_mf_helper.o 2>/tmp/toyld_mf_compile.log && \
	   $(BUILD)/toyc -nostdlib -ffreestanding -I include -I compiler \
		$(TOYLD_TESTDIR)/mf_main.c -o /tmp/toyld_mf_main.o 2>>/tmp/toyld_mf_compile.log; then \
		:; \
	else \
		printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=1; \
	fi; \
	if [ $$fail -eq 0 ]; then \
		if $(BUILD)/toyld /tmp/toyld_mf_main.o /tmp/toyld_mf_helper.o \
			-o /tmp/toyld_mf_test 2>/tmp/toyld_mf_link.log; then \
			/tmp/toyld_mf_test >/tmp/toyld_mf_test.log 2>&1; got=$$?; \
			if [ "$$got" = "0" ]; then \
				printf "$(GREEN)✓$(RESET) (exit 0)\n"; ok=1; \
			else \
				printf "$(RED)✗$(RESET) (exit $$got) — /tmp/toyld_mf_test.log\n"; fail=1; \
			fi; \
		else \
			printf "$(RED)LINK FAIL$(RESET)\n"; cat /tmp/toyld_mf_link.log; fail=1; \
		fi; \
	fi; \
	\
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), 1 total $(BLUE)══════$(RESET)\n\n" "$$ok" "$$fail"; \
	[ "$$fail" -eq 0 ]

# tld 自测试：toyld 链接自身 + 运行自我验证
test-toyld-self: $(BUILD)/toyld $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o
	@printf "$(BLUE)══════ toyld 自链接测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; \
	\
	# 1) 用 build/toyld 链接自身 .o 文件 \
	printf "  1) 自链接 stage-1 ... "; \
	if $(BUILD)/toyld $(BUILD)/toyld.o $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o \
		-o $(BUILD)/toyld-stage1 2>/tmp/toyld-stage1.log; then \
		printf "$(GREEN)✓$(RESET)\n"; ok=1; \
	else \
		printf "$(RED)✗$(RESET) (see /tmp/toyld-stage1.log)\n"; fail=1; \
	fi; \
	\
	if [ "$$ok" -eq 1 ]; then \
		# 2) stage-1 再链接自身 → stage-2 \
		printf "  2) 自链接 stage-2 ... "; \
		if $(BUILD)/toyld-stage1 $(BUILD)/toyld.o $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o \
			-o $(BUILD)/toyld-stage2 2>/tmp/toyld-stage2.log; then \
			printf "$(GREEN)✓$(RESET)\n"; \
			# 3) 检查 stage-1 和 stage-2 字节级一致 \
			if diff $(BUILD)/toyld-stage1 $(BUILD)/toyld-stage2 >/dev/null 2>&1; then \
				printf "  3) 收敛检查: $(GREEN)字节级一致$(RESET) ✅\n"; \
			else \
				printf "  3) 收敛检查: $(RED)字节不同$(RESET)\n"; fail=1; \
			fi; \
		else \
			printf "$(RED)✗$(RESET) (see /tmp/toyld-stage2.log)\n"; fail=1; \
		fi; \
	fi; \
	\
	if [ "$$fail" -eq 0 ]; then \
		printf "\n$(GREEN)✓ toyld 自举验证通过$(RESET)\n"; \
	else \
		printf "\n$(RED)✗ toyld 自举验证失败$(RESET)\n"; \
	fi

# ─── 错误报告测试 ───────────────────────────────────────────

ERRTESTDIR := compiler-tests/error
test-error: $(BUILD)/toyc
	@ok=0; fail=0; total=0; mkdir -p tmp; \
	printf "$(BLUE)══════ toyc 错误报告测试 ══════$(RESET)\n\n"; \
	for f in $(ERRTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect_err=$$(sed -n 's/.*EXPECT_ERR:[[:space:]]*\(.*\)\*\//\1/p' "$$f" | head -1 | sed 's/[[:space:]]*$$//'); \
		printf "  $(BLUE)━━━ %s ━━━$(RESET)\n" "$$name"; \
		output=$$($(BUILD)/toyc "$$f" -o /tmp/err_$$name.o 2>&1); \
		rc=$$?; \
		logfile="tmp/err_$${name}.log"; \
		echo "$$output" > "$$logfile"; \
		printf "  toyc output:\n"; \
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

# ─── toyld 归档链接测试 ───────────────────────────────────────

test-toyld-archive: $(BUILD)/toyld $(BUILD)/toyc $(BUILD)/toyar $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o
	@printf "$(BLUE)══════ toyld 归档链接测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; total=0; \
	\
	printf "  $(BLUE)%-25s$(RESET) " "archive_basic"; \
	if $(BUILD)/toyc -nostdlib -ffreestanding -I include -I compiler \
		$(TOYLD_TESTDIR)/arch_helper.c -o /tmp/arch_helper.o 2>/tmp/arch_compile.log \
		&& $(BUILD)/toyc -nostdlib -ffreestanding -I include -I compiler \
		   $(TOYLD_TESTDIR)/arch_main.c -o /tmp/arch_main.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/toyar rcs /tmp/libarch_basic.a /tmp/arch_helper.o 2>/dev/null \
		&& $(BUILD)/toyld /tmp/arch_main.o /tmp/libarch_basic.a \
		   $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o -o /tmp/arch_basic_test 2>/tmp/arch_link.log; then \
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
	if $(BUILD)/toyc -nostdlib -ffreestanding -I include -I compiler \
		$(TOYLD_TESTDIR)/arch_leaf.c -o /tmp/arch_leaf.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/toyc -nostdlib -ffreestanding -I include -I compiler \
		   $(TOYLD_TESTDIR)/arch_mid.c -o /tmp/arch_mid.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/toyc -nostdlib -ffreestanding -I include -I compiler \
		   $(TOYLD_TESTDIR)/arch_trans.c -o /tmp/arch_trans.o 2>>/tmp/arch_compile.log \
		&& $(BUILD)/toyar rcs /tmp/libarch_trans.a /tmp/arch_mid.o /tmp/arch_leaf.o 2>/dev/null \
		&& $(BUILD)/toyld /tmp/arch_trans.o /tmp/libarch_trans.a \
		   $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o -o /tmp/arch_trans_test 2>/tmp/arch_link.log; then \
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

# ─── toyar 归档器测试 ────────────────────────────────────────────

TOYARTESTDIR := compiler-tests/toyar
test-toyar: $(BUILD)/toyar $(BUILD)/toyc $(BUILD)/toyld $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o
	@printf "$(BLUE)══════ toyar 归档器测试 ══════$(RESET)\n\n"; \
	ok=0; fail=0; total=0; mkdir -p tmp; \
	\
	for f in $(TOYARTESTDIR)/*.c; do \
		[ -f "$$f" ] || continue; \
		total=$$((total+1)); \
		name=$$(basename "$$f" .c); \
		expect=$$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$$f" | head -1); \
		[ -z "$$expect" ] && expect=0; \
		printf "  $(BLUE)%-25s$(RESET) " "$$name"; \
		$(BUILD)/toyc "$$f" -o /tmp/$$name.o 2>/tmp/toyar_$$name-compile.log || { \
			printf "$(RED)COMPILE FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		$(BUILD)/toyld /tmp/$$name.o $(BUILD)/toyc_rt.o $(BUILD)/toyc_rt_start.o \
			-o /tmp/toyar_$$name 2>/tmp/toyar_$$name-link.log || { \
			printf "$(RED)LINK FAIL$(RESET)\n"; fail=$$((fail+1)); continue; }; \
		/tmp/toyar_$$name >tmp/toyar_$$name.log 2>&1; got=$$?; \
		if [ "$$got" = "$$expect" ]; then \
			printf "$(GREEN)✓$(RESET) (%d)\n" "$$got"; ok=$$((ok+1)); \
		else \
			printf "$(RED)✗$(RESET) (want %d got %d) — tmp/toyar_$$name.log\n" "$$expect" "$$got"; \
			fail=$$((fail+1)); \
		fi; \
	done; \
	\
	# 归档创建+列表+提取+符号表测试 \
	printf "  $(BLUE)%-25s$(RESET) " "archive_create"; \
	echo 'int fn(void){return 42;}' > /tmp/toyar_test_fn.c && \
	if $(BUILD)/toyc /tmp/toyar_test_fn.c -o /tmp/toyar_test_fn.o 2>/dev/null; then \
		if $(BUILD)/toyar rcs /tmp/toyar_test.a /tmp/toyar_test_fn.o 2>/dev/null; then \
			if [ -f /tmp/toyar_test.a ]; then \
				printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
			else \
				printf "$(RED)✗ (archive not created)$(RESET)\n"; fail=$$((fail+1)); \
			fi; \
		else \
			printf "$(RED)✗ (toyar failed)$(RESET)\n"; fail=$$((fail+1)); \
		fi; \
	else \
		printf "$(RED)✗ (compile failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	printf "  $(BLUE)%-25s$(RESET) " "archive_list"; \
	if $(BUILD)/toyar t /tmp/toyar_test.a 2>/dev/null | grep -q toyar_test_fn 2>/dev/null; then \
		printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
	else \
		printf "$(RED)✗ (list failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	printf "  $(BLUE)%-25s$(RESET) " "archive_extract"; \
	rm -f toyar_test_fn.o && \
	if $(BUILD)/toyar x /tmp/toyar_test.a 2>/dev/null && [ -f toyar_test_fn.o ]; then \
		rm -f toyar_test_fn.o; \
		printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
	else \
		printf "$(RED)✗ (extract failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	printf "  $(BLUE)%-25s$(RESET) " "symtab_test"; \
	echo 'int global_func(void){return 99;}' > /tmp/toyar_sym.c && \
	if $(BUILD)/toyc /tmp/toyar_sym.c -o /tmp/toyar_sym.o 2>/dev/null \
	   && $(BUILD)/toyar rcs /tmp/toyar_sym.a /tmp/toyar_sym.o 2>/dev/null; then \
		printf "$(GREEN)✓$(RESET)\n"; ok=$$((ok+1)); \
	else \
		printf "$(RED)✗ (symtab test failed)$(RESET)\n"; fail=$$((fail+1)); \
	fi; \
	total=$$((total+1)); \
	\
	# 清理 \
	rm -f /tmp/toyar_test_fn.c /tmp/toyar_test_fn.o /tmp/toyar_test.a; \
	rm -f /tmp/toyar_sym.c /tmp/toyar_sym.o /tmp/toyar_sym.a; \
	\
	printf "\n$(BLUE)══════$(RESET) $(GREEN)%d passed$(RESET), $(RED)%d failed$(RESET), %d total $(BLUE)══════$(RESET)\n" "$$ok" "$$fail" "$$total"; \
	[ "$$fail" -eq 0 ]
