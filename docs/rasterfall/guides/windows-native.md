# Windows Native Codex

> 状态：当前
> 文档更新：2026-09-27
> 源码核对基线：`windows/Makefile`、`windows/NativeCodex.ps1`、`tools/gpu_scene_play.ps1`、当前 `rasterfall_options.c`

这是 Rasterfall 当前主要且优先的开发、构建编排、GPU 实机验证和签收 lane。唯一入口是
`windows/NativeCodex.ps1`；它固定使用 MSYS2 `mingw64` + `usr/bin`，将对象、exe
和 package 放入 `build-windows/`，并且始终从 `build-windows/rasterfall-windows`
运行真实 `rasterfall.exe`。

共享 C 源码与 freestanding Linux 路径继续保留，但 WSL 现在只属于辅助/历史兼容环境，不保证同步更新、
可构建或结果正确。WSL/llvmpipe/hosted Vulkan 可以帮助定位纯逻辑或 ABI 问题，不能替代 Windows native
present、物理 GPU 驱动、Win32/SDL 窗口生命周期和性能证据。

## 最小软件

- Git for Windows；PowerShell 5+；Python 3。
- 一套 MSYS2，安装 `make`、`mingw-w64-x86_64-gcc`、`binutils`；默认根目录为
  `C:\msys64`，其他位置设置 `RF_WINDOWS_MSYS2_ROOT`。
- 静态 SDL2 二选一：仓库现有 `.windows-deps/x86_64-w64-mingw32`，或 MSYS2 原生
  `C:\msys64\mingw64`。后者可直接通过 `pacman -S mingw-w64-x86_64-SDL2`
  安装；wrapper 会自动优先使用仓库 staging，缺失时使用 MSYS2 原生 prefix。
  现有 `scripts/setup-windows-build.sh` 是 Debian/WSL 依赖引导器，不是必需步骤。
- Windows Vulkan loader、Intel/其他物理 GPU 驱动。无需为运行时额外安装 Vulkan
  SDK；仓库 backend 动态加载 `vulkan-1.dll`。

## 日常闭环

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

`gpu-test` 使用 `--renderer gpu-scene`，该入口自动要求 native GPU；Scene 初始化或提交失败会得到
非零退出码。逐帧 `SCENE-SOURCE` 与 `SCENE-NATIVE` 应报告零旧命令、零 mixed draw、零 bridge 和零常规读回；
`--frame-audit` 和 `rasterfall.log` 是诊断证据。所有运行都从 package root 进行，避免裸 exe 误把
缺失资产误判为 GPU 问题。`run` 可携带任意当前 CLI 参数，例如：

```powershell
.\windows\NativeCodex.ps1 run --help
```

系统 GPU 枚举可能因 CIM/PnP 权限不可用；`doctor` 会将其报告为非致命的枚举缺口，不以此推断 Vulkan
不可用。物理 GPU 事实仍以 required native GPU 测试打印的 adapter、退出码和帧审计为准。

## WIN-DEV-1 验收

Windows native 验收以当前构建的 CLI 输出和退出码为准。原 GPU 堆损坏已修复为
retained command 跨帧容量失配；完整生命周期组合仍无同一份实机证据。
普通构建的完整 `--logic-test` 曾受默认 Windows 主线程栈容量限制，以
0xC00000FD 退出，与 GPU 堆越界不同。正式 `windows/Makefile` 已将栈 reserve 设为
16 MiB，聚合逻辑测试在正式链接配置下通过。

在真实 Windows 物理 GPU（目标为 Intel）机器上，`doctor` 检查依赖；`package` 包含 exe、
公开资产及本地私有资产（若存在）；`test` 运行逻辑回归；`gpu-test` 要求连续独立 Scene native 提交，
且无旧命令、mixed draw、bridge 或常规读回；`acceptance` 生成 normal-frame audit BMP、
visual capture BMP 和 package 内 `rasterfall.log`。这些命令的当前退出码和生成物才是本次
验证结果；Linux `build/` 不应因 Windows 构建产生或复用对象。

## 专项验证

Scene 互动、world cycle、combat、resize、故障注入和连续运行使用 `tools/gpu_scene_play.ps1 -Stage All`。
开启 Vulkan validation 时，除 `VK_INSTANCE_LAYERS` 外还需把本地 MinGW layer DLL 及运行时目录放入 PATH，并核对 loader 与 Synchronization 日志。当前流程见 [GPU 验收与诊断](gpu-validation.md)；RB-0 旧流程见[退役归档](../archive/gpu-compute-retirement/README.md)。

## 留到后续

不在 WIN-DEV-1 中做 CMake/Ninja 重构、SDL-free 平台层、跨 ABI 兼容、自动 GPU
驱动安装、CI 实机调度，或 renderer/gameplay 语义修改。后续可单独改善原生依赖
安装器、Vulkan adapter 选择诊断和更完整的 resize/near-mid-far acceptance 矩阵。
