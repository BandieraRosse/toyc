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
#              链接，暂跳过。编译检查已覆盖） ─────────────────
_SRCS_string  := string.c

# ── core — 系统调用封装，6 个源文件 ──────────────────────────────
_SRCS_core    := core/io.c core/mem.c core/proc.c \
                 core/signal.c core/sync.c core/time.c

# ── stdio — 格式化 I/O
# 注意：printf.c/snprintf.c 使用 __builtin_va_arg，toyc 编译后运行时 segfault
#       （已知 toyc cgen va_arg bug）。功能测试暂跳过，编译已覆盖。 ──
_SRCS_stdio   := stdio/printf.c stdio/scanf.c stdio/snprintf.c
_DEPS_stdio   := core string ctype

# ── time — 时间函数 ───────────────────────────────────────────────
_SRCS_time    := time.c
_DEPS_time    := core

# ── misc — 多样的工具函数 ─────────────────────────────────────────
_SRCS_misc    := misc/assert.c misc/envp.c misc/file.c \
                 misc/path.c misc/system.c
_DEPS_misc    := core

# ── net — 网络相关 ────────────────────────────────────────────────
_SRCS_net     := net/socket.c net/dns.c
_DEPS_net     := core

# ── poll — I/O 多路复用 ──────────────────────────────────────────
_SRCS_poll    := poll.c
_DEPS_poll    := core

# ── tty — 终端控制 ────────────────────────────────────────────────
_SRCS_tty     := tty.c
_DEPS_tty     := core

# ── procfs — 进程文件系统读取 ─────────────────────────────────────
_SRCS_procfs  := procfs.c
_DEPS_procfs  := core

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
