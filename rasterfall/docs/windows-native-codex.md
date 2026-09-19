# Windows Native Codex

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 `rf_core_host.c` retained WORLD partition 同步实际分配容量；跨帧缩小/增长回归覆盖缓存复用。
> 源码核对基线：`windows/Makefile`、`windows/NativeCodex.ps1`、当前 `rasterfall_options.c`

这是 Rasterfall 的 Windows 原生开发 lane，不替代 Linux/WSL。唯一入口是
`windows/NativeCodex.ps1`；它固定使用 MSYS2 `mingw64` + `usr/bin`，将对象、exe
和 package 放入 `build-windows/`，并且始终从 `build-windows/rasterfall-windows`
运行真实 `rasterfall.exe`。

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

`gpu-test` 使用 `--gpu-required --gpu-native-present`，因此 CPU fallback、software
present、readback/copy 或 native GPU 初始化失败都会得到非零退出码；`--frame-audit`
和 `rasterfall.log` 是诊断证据。所有运行都从 package root 进行，避免裸 exe 误把
缺失资产误判为 GPU 问题。`run` 可携带任意当前 CLI 参数，例如：

```powershell
.\windows\NativeCodex.ps1 run --help
```

## WIN-DEV-1 验收

Windows native 验收以当前构建的 CLI 输出和退出码为准。原 GPU 堆损坏已修复为
retained command 跨帧容量失配；完整生命周期和 WIN-DEV-1 最终签收仍待完成。
普通构建的完整 `--logic-test` 曾受默认 Windows 主线程栈容量限制，以
0xC00000FD 退出，与 GPU 堆越界不同。正式 `windows/Makefile` 已将栈 reserve 设为
16 MiB，聚合逻辑测试在正式链接配置下通过。

在真实 Windows 物理 GPU（目标为 Intel）机器上，`doctor` 无 required failure；
`package` 成功并包含 exe、`rasterfall/assets`，以及本地 `private-assets`（若存在）；
`test` 退出码为 0；`gpu-test` 退出码为 0 且 audit 明确为 `gpu-native`、无 CPU
fallback/readback/copy；`acceptance` 生成 normal-frame audit BMP、visual capture
BMP 和 package 内 `rasterfall.log`，三者命令退出码均为 0。Linux `build/` 不应因
Windows 构建产生或复用对象。

## 留到后续

不在 WIN-DEV-1 中做 CMake/Ninja 重构、SDL-free 平台层、跨 ABI 兼容、自动 GPU
驱动安装、CI 实机调度，或 renderer/gameplay 语义修改。后续可单独改善原生依赖
安装器、Vulkan adapter 选择诊断和更完整的 resize/near-mid-far acceptance 矩阵。
