# libs.mk — Tinylibc 库模块声明
#
# 用法：被 Makefile 的 test-lib 目标 include。
#
# 添加新库：在 LIBS 后追加名称，定义 _SRCS_<name>，可选定义 _DEPS_<name> 和 _TEST_<name>。
#
# 变量约定：
#   _SRCS_<name>  — 源文件列表（相对于 ../Tinylibc/lib/）
#   _DEPS_<name>  — 链接时依赖的其他 lib 模块（空格分隔）
#   _TEST_<name>  — 测试驱动名（compiler-tests/lib/test_<name>.c，不含 .c 后缀）
#                   不定义则跳过功能测试（仅编译检查）

LIBS := math ctype string core stdio time misc net poll tty procfs evdev_kbd evdev_mouse audio

# ── math — 纯数值计算，无系统调用 ──────────────────────────────────
_SRCS_math    := math/math.c
_TEST_math    := test_math

# ── ctype — 字符分类，纯逻辑 ──────────────────────────────────────
_SRCS_ctype   := ctype.c
_TEST_ctype   := test_ctype

# ── string — 字符串操作（stderror 内部调用了 snprintf，功能测试需要 stdio
#              链接，strerror(0-40) 返回静态字符串无需 snprintf，故测试独立通过） ──
_SRCS_string  := string.c
_DEPS_string  := stdio ctype
_TEST_string  := test_string

# ── core — 系统调用封装，6 个源文件 ──────────────────────────────
_SRCS_core    := core/io.c core/mem.c core/proc.c \
                 core/signal.c core/sync.c core/time.c
_DEPS_core    := string stdio ctype
_TEST_core    := test_core

# ── stdio — 格式化 I/O
# Bug fix: __builtin_va_start 用了 forward linear search 而非 scope-aware
# SEARCH_LOCAL，当 if 分支和外部同时声明 args 变量时 va_start 用内层偏移
# 而函数参数传参用外层偏移，导致 segfault。同时修复了大结构体参数传递的
# 64 位指针截断和 AST_VAR 指针加载问题。
# 已知 3/17 功能测试失败（%X 大写、%u 大值、%d 负数，数值格式化问题）。 ──
_SRCS_stdio   := stdio/printf.c stdio/scanf.c stdio/snprintf.c
_DEPS_stdio   := core string ctype
_TEST_stdio   := test_stdio

# ── time — 时间函数 ───────────────────────────────────────────────
_SRCS_time    := time.c
_DEPS_time    := core stdio string ctype
_TEST_time    := test_time

# ── misc — 多样的工具函数 ─────────────────────────────────────────
_SRCS_misc    := misc/assert.c misc/envp.c misc/file.c \
                 misc/path.c misc/system.c
_DEPS_misc    := core stdio string ctype
_TEST_misc    := test_misc

# ── net — 网络相关 ────────────────────────────────────────────────
# 功能测试 test_net.c 覆盖 socket syscall、纯逻辑（htons/inet_addr/inet_ntoa）和 DNS 解析函数
_SRCS_net     := net/socket.c net/dns.c
_DEPS_net     := core string stdio ctype
_TEST_net     := test_net

# ── poll — I/O 多路复用 ──────────────────────────────────────────
_SRCS_poll    := poll.c
_DEPS_poll    := core
_TEST_poll    := test_poll

# ── tty — 终端控制 ────────────────────────────────────────────────
_SRCS_tty     := tty.c
_DEPS_tty     := core
_TEST_tty     := test_tty

# ── procfs — 进程文件系统读取 ─────────────────────────────────────
_SRCS_procfs  := procfs.c
_DEPS_procfs  := core stdio string ctype
_TEST_procfs  := test_procfs

# ── evdev_kbd — 键盘输入设备 ─────────────────────────────────────
_SRCS_evdev_kbd   := evdev_kbd.c
_DEPS_evdev_kbd   := core

# ── evdev_mouse — 鼠标输入设备 ────────────────────────────────────
_SRCS_evdev_mouse := evdev_mouse.c
_DEPS_evdev_mouse := core string

# ── audio — ALSA 音频 ────────────────────────────────────────────
_SRCS_audio   := audio/alsa.c
_DEPS_audio   := core

# ── 以下 lib 因架构限制跳过：
#   init/    — 含 init.c + start.S（汇编），需要 toyas/gas
#   thread/  — 含 TLS 和 clone.S，需要 gcc
