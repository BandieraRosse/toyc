# 构建、平台与验证

> 文档更新：2026-09-03
> 源码核对基线：`75a10cd`（将项目协作说明转向 Rasterfall）

## Linux

根 `Makefile` 的 Rasterfall 区域定义全部独立编译单元、依赖和链接对象。`make app-rasterfall`
构建 freestanding Linux 程序，窗口/输入/渲染/音频来自仓库 Tinylibc 与公共库。默认运行时读取
`rasterfall/assets`；`rasterfall-embedded` 才嵌入公开资源。

平台相关实现主要是：

- `lib/platform/window_wayland.c`、`lib/graphics/wayland_min.c`：Wayland 窗口与协议。
- `lib/graphics/renderer.c`、`lib/input/input.c`：共享软件渲染器和输入状态。
- `lib/audio/audio.c`、`lib/audio/alsa.c`、`lib/audio/pulse_min.c`：Linux 音频抽象及 ALSA/Pulse 后端。
- `compiler/toyc_rt.c`、`lib/`、`include/tlibc/`：freestanding 运行时和 libc。

## Windows

`windows/Makefile` 用 MinGW-w64 + SDL2 构建相同玩法/渲染源，并加入 `windows/src/` 的 runtime、
WinSock、SDL 窗口/音频、线程和 WinMain 适配。平台契约头在 `windows/include/`。资源定位和包结构见
`windows/README.md`；不要把 Windows 修复硬编码进共享玩法，优先修平台适配层。

## 改文件列表时

新增 Rasterfall `.c` 文件通常必须同时加入根 Makefile 的对象/规则及 `windows/Makefile` 的源列表；
若该文件要求 Toyc 自托管，还要核对 self 对象规则。新增公开运行时资源要检查默认文件加载、内嵌资源
依赖和 Windows package 的复制规则。

## 最小验证矩阵

- 纯玩法/session/map：`make app-rasterfall`，再运行 `build/rasterfall --logic-test`。
- 渲染或模型：构建 + logic test，并使用相关 dump/benchmark/诊断参数；涉及画面时做实际启动检查。
- 网络：先跑 logic test 中的 packet/pipeline 用例，再按 `network-architecture.md` 做所需人工拓扑。
- Linux 平台：实际 Wayland/ALSA 启动；无图形/音频环境时明确报告未覆盖项。
- Windows 平台或共享平台契约：依赖已准备时运行 `make win-rasterfall`；打包变化再跑 package。

具体可用命令以 `rasterfall/README.md`、根 README 和 `--help` 为准。不要在本导航记录会变化的测试
通过数量。Rasterfall 不要求由 Toyc 编译；除非改动触及工具链或公共自托管路径，不必扩大到编译器全套测试。
