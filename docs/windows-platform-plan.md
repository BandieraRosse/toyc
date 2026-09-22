# Toyc Windows Platform V1：计划总览与 Checkpoint 进度

文档更新：2026-09-18（Checkpoint 4 实现完成）
状态基线：以根 `Makefile`、`windows/Makefile`、`make platform-sources` 和实际构建输出为准。

## 目标与边界

Toyc 保留 Linux syscall-first 主线；Windows V1 是同一套 Toyc userland
contract 的 Win32 实现。V1 首先覆盖 UTF-8 路径、Toyc fd、错误映射和基本文件系统，
不复制 Linux libc，也不做 fork/POSIX 模拟、PE/COFF 输出或 Windows 自托管。

本计划只描述 userland platform boundary。Rasterfall 的 SDL/Win32 runtime 仍是另一套
平台层，不因本计划被强行并入通用 userland libc。

## 冻结的共同 contract（CP1）

共同声明位于 [`include/toyc_platform_contract.h`](../include/toyc_platform_contract.h)：

- 所有 portable 路径输入是 UTF-8，内部路径分隔符是 `/`。
- 平台失败统一返回负 Toyc errno；不得把宿主 `errno` 或裸 Win32 error 暴露给 portable app。
- fd `0/1/2` 固定为 stdin/stdout/stderr；新 fd 是非负 Toyc int。
- CP1/文件 V1 的 `openat` 接口接受 `AT_FDCWD`；其他 dirfd 保留给后续目录句柄实现，
  不得静默忽略。
- CP1 只冻结 `__read`、`__write`、`__openat`、`__close` 的声明和 ABI 形状；实现仍由
  平台 source set 提供。

`include/core.h` 继续作为现有 Linux 应用入口，但其公共 I/O 声明来自上述 contract；
Linux-only 的 syscall、UAPI 结构、进程、mmap、epoll、信号和目录 ABI 仍留在 Linux 入口。
Windows shadow `core.h` 只补 Windows 侧扩展声明，不再为共同 I/O 声明另一套类型。

## Source set 与 provider 规则

Linux 根 Makefile 显式维护：

- `LIBC_PORTABLE_*_SRCS`：共同实现；CP3 已纳入 string、ctype、math 和 snprintf。
- `LIBC_LINUX_*_SRCS`：Linux-native 实现。
- `LIBC_REPLACEMENT_*_SRCS`：替换 provider（CP1 当前为空）。
- `APP_PORTABLE_SRCS` 与 `APP_LINUX_SRCS`：portable app 和 Linux-native app。

Windows Makefile 显式维护 `WINDOWS_PORTABLE_LIB_SRCS`、
`WINDOWS_NATIVE_LIB_SRCS`/`WINDOWS_REPLACEMENT_LIB_SRCS`、`PORTABLE_APP_SRCS` 和
`WINDOWS_NATIVE_APP_SRCS`。执行 `make platform-sources` 会打印两个平台最终选择的 source
set 和共同 I/O provider；构建会检查每个平台 source set 没有重复条目。

对象名保留源路径，例如：

```text
app/portable/coreutils/cat.c -> build/app/portable/coreutils/cat.o
lib/linux/core/io.c         -> build/lib/linux/core/io.o
app/portable/coreutils/cat.c -> build/windows/app/portable/coreutils/cat.o
```

因此新增同 basename 文件不会静默覆盖另一个对象；公共 `app-<name>` 目标仍会在 basename
重复时直接报错。Windows 当前共同 I/O provider 是 `lib/windows/core/io.c`，CP2 已将其
替换为 Win32 W API 实现。

## Checkpoint 总览

| Checkpoint | 目标 | 非目标 | 验收重点 | 状态 |
|---|---|---|---|---|
| CP1 | 冻结 contract、source set、provider 选择；保留对象路径；self-app 纳入 portable | 不实现更多 Win32 API | Linux/Windows header-only compile；source selection；`app-cat`、`self-app-cat` | **完成（见实际结果）** |
| CP2 | UTF-8↔UTF-16、Toyc fd→HANDLE、Win32 W I/O、错误映射、argv shim | 目录、socket、fork、mmap | Windows cat 的 ASCII/中文/空格路径、重定向、binary 和统一 `-ENOENT` | **实现完成；Wine 行为验收受环境阻塞** |
| CP3 | portable libc 基础；hexdump/touch/echo/cp | ls、grep-r、网络 | 两平台输出一致；partial I/O 和大文件 | **完成（Wine 动态验收未覆盖）** |
| CP4 | metadata、cwd、路径修改、稳定目录 iterator；迁移 pwd/mkdir/mv/rm/rmdir/grep/ls/fcount | uid/gid/mode、symlink、ACL 完全等价 | Unicode 目录树、递归 grep、rename/delete 错误 | **实现完成（Windows 动态验收受环境限制）** |
| CP5 | Windows-hosted Toyc compiler runtime spike，仍只产 ELF64 | PE/COFF、Windows native self-host、cross linker | 同输入 Linux/Windows compiler 输出可比 | 未开始 |

## CP1 实施记录

### 已完成

- 新增共同 contract 头，统一 negative errno、UTF-8 path、fd 和 `AT_FDCWD` 语义。
- Linux/Windows `__read`、`__write`、`__openat`、`__close` 声明形状一致；CP2 的 Win32
  HANDLE、UTF-16 转换和错误映射详见下方记录。
- 根 Makefile 和 Windows Makefile 分开列出 portable/platform/replacement source set。
- Linux 与 self-host library/app object 映射改为保留源路径；basename 冲突会显式失败。
- `SELF_APP_SRCS` 纳入 `app/portable`，`self-app-cat` 目标存在并成功生成。
- 新增 `tests/platform/toyc_platform_header.c`，`make test-platform-contract` 用 GCC 和
  `x86_64-w64-mingw32-gcc` 对共同 contract 做 `-fsyntax-only` 检查。
- `make platform-sources` 输出 Linux/Windows 最终 source set 与单 provider 说明。

### 实际验证（2026-09-18）

- `make test-platform-contract`：通过（Linux GCC、Windows MinGW 均通过）。
- `make platform-sources`：通过；source set 无重复，输出两个平台 provider。
- `make app-cat`：通过；生成 `build/cat`，对象路径保留为
  `build/app/portable/coreutils/cat.o`。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp1-win-clean2 app`：CP1 基线构建通过；CP2
  新实现使用 `/tmp/toyc-cp2-win/windows/cat.exe` 验证，详见下方记录。
- `make BUILD=/tmp/toyc-cp1-linux-clean2 app-cat`：通过干净构建；生成
  `/tmp/toyc-cp1-linux-clean2/cat`。
- `make self-app-cat`：通过重新编译并生成 `build/cat_self`；随后读取
  `docs/README.md` 的输出与原文件逐字节一致。`tlibc_types.h` 的既有 64 位宽度断言仍由
  Linux/Windows hosted GCC 执行；Toyc 尚不支持 `sizeof` 数组边界，因此自托管预处理时
  跳过这两条 hosted-build 断言，没有为此修改编译器。

### 未解决风险与明确后续

- Windows common header 的 MinGW include 顺序仍需在后续 metadata/目录接口阶段继续收敛；
  CP1/CP2 已保证 contract header 与 userland app 的编译选择明确。
- self-app 链接已改为按需扫描归档，使 cat 不被无关 Rasterfall renderer 拖入；其他已有
  self-app 的兼容性仍应在后续批量验证中单独检查。
- CP1 基线时 `lib/portable` 为空；CP3 已迁移 string/ctype/math/snprintf 等实现，详见下方记录。
- 未运行完整 Linux app 全量构建、Rasterfall 或 Windows Rasterfall；这些不属于 CP1/CP2
  userland 最小验收。

每完成一个 checkpoint，应在本表更新状态，并在对应实施记录中追加实际命令、结果和未解决
风险；不以设计目标代替实测结论。

## CP2 实施记录

### 已完成

- `lib/windows/core/io.c` 已改为 Win32 W API provider：动态 `MultiByteToWideChar`
  UTF-8→UTF-16、`CreateFileW`、`ReadFile`、`WriteFile`、`CloseHandle`。
- 建立统一 Toyc fd 表，预置 0/1/2，区分 FILE/CONSOLE，使用 `CRITICAL_SECTION` 保护；
  SOCKET kind 已保留在同一表中供后续网络阶段扩展。
- `openat` 只接受 `AT_FDCWD`，其他 dirfd 返回 `-ENOSYS`；支持读写模式、创建、截断、
  追加、binary，以及 read/write/delete sharing。ReadFile/WriteFile 保留 partial count。
- Win32 error 映射集中在 `windows/include/toy_windows_errors.h`，对外返回固定负 Toyc
  errno；增加 Linux-hosted pure-logic mapping test。
- 新增 `windows/src/userland_winmain.c`，通过 MinGW `wmain` 动态转换 UTF-8 argv；
  Windows app 使用 `-municode`，Rasterfall `wWinMain` 路径未改动。
- 新增 `windows/Makefile pe-audit`，检查 PE imports 包含 W API，而不把 CRT I/O 当作 provider。

### 实际验证（2026-09-18）

- `make -f windows/Makefile BUILD=/tmp/toyc-cp2-win app`：通过交叉编译和链接。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp2-win pe-audit`：通过，确认导入
  `CreateFileW`、`ReadFile`、`WriteFile`、`CloseHandle`、`MultiByteToWideChar`、
  `WideCharToMultiByte`。
- `make test-windows-core-logic`：通过，覆盖 FILE_NOT_FOUND、ACCESS_DENIED、FILE_EXISTS、
  DISK_FULL 和未知错误映射；同时覆盖 ERROR_NO_UNICODE_TRANSLATION→EINVAL、
  ERROR_DIRECTORY→ENOTDIR、ERROR_BROKEN_PIPE→EPIPE，以及 DWORD 单次 I/O clamp 边界。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp2-win2 app`：通过最新实现的 clean cross build。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp2-win2 pe-audit`：通过，确认 W API imports。
- Wine 行为验收仍未覆盖：尝试设置 `XDG_RUNTIME_DIR=/tmp/toyc-cp2-xdg`、
  `WINEPREFIX=/tmp/toyc-cp2-prefix`（目录 0700），Wine 仍尝试在 `/run/user/1000/wine`
  创建目录并因只读文件系统失败；因此 ASCII/中文/空格路径、重定向和 binary 字节级验收
  需在可运行 Wine/Windows 环境补跑。

### 未解决风险

- `mode` 在 CP2 仍只接收、不模拟 Unix ACL/mode；目录、metadata、socket、poll、线程和
  其他 native API 留到后续 checkpoint。
- fd 表容量当前固定为 256；这是表容量而非路径缓冲限制，UTF-16 路径转换仍按实际长度动态
  分配。
- `O_APPEND` 使用 Win32 `FILE_APPEND_DATA` desired access，由 Windows append-handle 语义
  负责定位，避免 `SetFilePointer+WriteFile` 的跨进程竞态。
- ReadFile/WriteFile 单次请求超过 DWORD 时 clamp 到 `0xffffffff`，不会回绕；fd 表锁不再
  覆盖阻塞 I/O，每个 entry 有独立 I/O 锁，close 通过 entry 锁与 in-flight I/O 协调。
- anonymous pipe 的 `ERROR_BROKEN_PIPE` 在 `__read` 中按 Toyc/POSIX 约定返回 EOF `0`；
  `__write` 仍通过统一映射返回 `-EPIPE`。

## CP3 实施记录

### 已完成

- 将 string、ctype、math 和 snprintf 四个 libc 单元真正迁入 `lib/portable`，删除无用
  Linux syscall/core include；Makefile 自动 source discovery
  确保每个实现只有一个 provider。
- 将 `hexdump`、`touch`、`echo`、`cp` 迁入 `app/portable`，与 `cat` 一样由 Linux 和
  Windows 选择同一份源文件；四个 app 改为只依赖 common I/O contract，不使用
  `__printf` 或 Linux `stat` 偶然提供的声明。
- `echo` 现在只按参数以空格分隔输出并追加换行，不再解释 `>` 或自行打开目标文件。
  `touch` 的行为明确为 CP3 既有的 create+truncate，不模拟 POSIX 的 timestamp touch。
- `cp` 改为流式 `read`/partial `write` 循环，正确处理超过 4096 字节、空文件、binary
  数据、读写错误和两端 close；不提前依赖 CP4 metadata/fstat。
- 根 Makefile 新增 `test-portable-coreutils`，覆盖 binary、3-buffer 大文件、空文件、
  touch 截断、echo 输出和 hexdump；`write_all` 循环是 partial-write contract 的 app 侧
  验证路径。Windows source selection/PE audit 继续由 Windows Makefile 提供。

### 实际验证（2026-09-18）

- `make test-portable-coreutils`：通过；Linux freestanding app 实际完成 binary、12288
  字节大文件、空文件、截断和输出检查。
- `make test-portable-io-logic`：通过；hosted pure-logic fake writer 每次只接受 7 字节，
  验证 `write_all` 在 partial write 下不丢字节，并覆盖 zero-progress 失败和空写入。
- `make self-app-cat self-app-hexdump self-app-touch self-app-echo self-app-cp`：通过；
  Toyc 编译和 self-host link 均生成对应 `_self` 可执行文件。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp3-win app`：通过干净 MinGW cross build，
  生成 cat/cp/echo/hexdump/touch 五个 PE；仅保留编译器/CRT 启动依赖，portable app 未
  增加 Windows 副本。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp3-win platform-sources`：通过；portable
  libc 为四个迁移源，Windows replacement 仍只有 `lib/windows/core/io.c`，app source
  无重复。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp3-win-llp64-clean app`：通过 clean Windows
  userland build，`APP_CFLAGS` 启用 `-Werror`；迁移后的 portable libc 无 LLP64/builtin
  prototype warning。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp3-win-llp64-clean pe-audit`：通过；cat PE 导入
  `CreateFileW`、`ReadFile`、`WriteFile`、`CloseHandle`、UTF 转换 W API。
- `make -j$(nproc) -f windows/Makefile BUILD=/tmp/toyc-cp3-win-rasterfall all`：通过；Rasterfall
  Windows 专用构建已改用 `lib/portable/math.c`，并修正 legacy runtime provider 的 common
  I/O 函数签名以匹配 contract。该构建仍是 SDL/Win32 Rasterfall 路径，不等同于 portable
  userland V1。
- `make test`：61/62 通过，既有 `38_cast_long_arg` compile failure 仍存在；本次 CP3
  没有修改 compiler。该失败不由 portable app/lib 迁移证实为回归。
- `make app-test_string app-test_printf` 后运行 `build/test_string` 与 `build/test_printf`：
  通过（string 71/71；snprintf/printf 87/87）。新增用例覆盖超过 32-bit 的 `%lld`、`%llu`、
  `%llx` 和 `%p`，用于锁定 LLP64 下不截断的承载类型。
- `make self-app-cat self-app-hexdump self-app-touch self-app-echo self-app-cp`：通过；
  LLP64 修正后 Toyc self 编译和链接仍成功。
- `make app-rasterfall`：已恢复 Rasterfall 独立主源 `rasterfall/src/rasterfall.c` 的显式
  `build/rasterfall.o` 编译/链接规则，并完成构建；该对象不进入 portable app discovery。
- `build/rasterfall --logic-test`：通过，输出 `rasterfall: logic test passed`。本次运行使用
  CPU renderer、资产目录和 headless logic path，未宣称覆盖窗口/音频交互。
- Wine 动态行为验收（Windows binary、Unicode/空格路径和重定向）本轮未执行；CP2 已记录
  Wine 在受限环境中因 `/run/user/1000/wine` 只读失败。本轮有 clean cross build 和 PE
  import audit，但没有把它们表述为 Wine 行为等价证明。

### 未解决风险

- Windows userland portable libc 的 `size_t`/`ssize_t` 已按 LLP64 使用 64-bit 类型，
  string 与 snprintf 已通过 `-Werror` clean build。`off_t`、其他 native ABI 的完整宽度
  收敛仍属于后续 metadata/host-runtime 风险，不在本轮扩大。
- `%p` 与 `%llx` 现在使用 64-bit unsigned hex formatter；`%lx` 仍按 `unsigned long`
  取参后扩宽，保持各格式的 vararg contract。
- `hexdump` 保留原有展示格式的 ANSI 转义；CP3 只验证输出存在和 I/O，未承诺跨 console
  的颜色显示一致。
- partial-write 测试是 hosted fake-writer contract 测试，不是 Linux syscall provider
  的故障注入；Windows provider 的 partial count 由 CP2 约束，真实 Windows pipe/file
  动态故障注入仍留待后续测试 seam。

## CP4 实施记录（实现完成；Windows 动态验收受环境限制）

### 已实现的边界

- `include/toyc_platform_contract.h` 新增稳定的 `toyc_file_info`（type、size、mtime）和
  `toyc_dir_iterator`/`toyc_dir_entry`；目录记录只暴露 UTF-8 名称和 Toyc 文件类型，不暴露
  `linux_dirent64`、`struct stat` 或 Win32 `FindData`。
- 新增 `toyc_lseek`、`toyc_fstat`、`toyc_stat`、cwd/chdir、mkdir/unlink/rmdir/rename 和
  目录迭代接口。Linux provider 使用现有 syscall/raw getdents64 适配；Windows provider
  使用 HANDLE metadata、动态 UTF-16、`FindFirstFileW`/`FindNextFileW` 和 W 路径操作。
- Windows rename 使用 `MOVEFILE_REPLACE_EXISTING`，删除/readonly/非空目录错误继续经过
  固定 Toyc errno；不承诺 Unix mode、uid/gid、ACL 或 symlink 等价。
- `pwd`、`mkdir`、`mv`、`rm`、`rmdir`、`grep`、`ls`、`fcount` 已迁入 `app/portable`，
  均使用共同 file-info/directory iterator；没有创建 Windows app 副本。`ls -l` 的展示格式
  只使用稳定的 type/size/mtime/name 字段，不承诺 Unix mode/uid/gid 等价。
- `toyc_lseek` 在 common ABI 使用固定宽度 `int64_t` offset/return；Windows rename 只使用
  `MOVEFILE_REPLACE_EXISTING`，不把跨卷操作静默转换为 copy+delete。Linux `DT_UNKNOWN`
  目录项由 grep/fcount 通过 `toyc_stat` 补齐类型。

### 实际验证（2026-09-18）

- Linux `make app-pwd app-mkdir app-mv app-rm app-rmdir app-grep app-ls app-fcount`：通过。
- `make test-portable-filesystem`：通过；可重复覆盖 Unicode 目录、目录/普通文件类型和大小、
  递归 grep、非空目录拒绝、rename-over-existing、删除文件和空目录删除。
- Linux Unicode 目录树行为：通过；`pwd`、递归 grep、rename-over-existing、文件删除和
  空目录删除均实际运行。只读错误未在 root 运行环境中宣称通过（root 权限会绕过部分
  权限拒绝）。
- `make test-platform-contract`：Linux/MinGW contract header 检查通过。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp4-win-final2 app`：通过，CP4 portable app
  （含 `ls`/`fcount`）与 Win32 provider 在 `-Werror` 下 clean cross build。
- `make -f windows/Makefile BUILD=/tmp/toyc-cp4-win-final2 pe-audit`：通过；cat 验证文件
  W API，grep 验证 metadata、目录、cwd、rename/delete 的 W API imports。
- self-host：新增 portable app 的 Toyc 编译和链接目标均已生成（包括 `pwd/mkdir/mv/rm/rmdir/grep`）；
  `ls`/`fcount` 已进入同一 source set，当前仅完成 hosted/Windows clean build 验证，未重复进行耗时的
  self-host 启动验收；
  `build/pwd_self` 已实际运行通过。其余 self-app 仅做启动/链接级验收，未宣称完整行为等价。
- Wine/Windows 动态验收（中文目录、rename-over-existing、readonly error）仍未覆盖；
  CP4 的实现边界已完成，但不能将 clean cross build 当作 Windows 运行时等价证明。

### 未解决风险

- Windows fd 表当前仍以文件/console HANDLE 为主；目录 iterator 使用独立 `FindFirstFileW`
  handle，不把目录结构纳入 portable ABI。后续若需 `openat` 相对目录 fd，需另立 contract。
- Linux raw getdents64 仍完整保留给 Linux-native caller；portable `grep`/`ls`/`fcount` 不依赖它。
- `toyc_file_info.type` 只承诺 regular/directory/other；reparse point、symlink、ACL、mode
  仍不做跨平台模拟。
