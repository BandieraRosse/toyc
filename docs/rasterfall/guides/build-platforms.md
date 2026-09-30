# 构建、平台与验证

> 状态：当前
> 所有者：Rasterfall 构建与平台
> 最近核对：2026-09-26

Rasterfall 的主开发和验收环境是 Windows 原生 PowerShell、MSYS2/MinGW 与物理 GPU。入口为 `windows/NativeCodex.ps1`；具体依赖、等待和日志规则见[Windows Native](windows-native.md)。Linux/WSL 可以辅助编译或诊断，但不代替 Windows native present、驱动、窗口生命周期和性能结论。

## Windows 主闭环

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

`build` 生成 Windows 玩家程序，并在已有运行目录时同步其中的 exe；`run`、`test`、`gpu-test` 和 `acceptance` 更新运行目录的 exe 与资源，不创建 zip。仅显式执行 `package` 才生成 `build-windows/rasterfall-windows.zip`。`test` 运行逻辑回归；`gpu-test` 在运行目录中执行 120 帧硬件 Scene；`acceptance` 增加 normal-frame 与视觉 capture。`run` 可把额外参数传给运行目录中的 `rasterfall.exe`。命令可用性和具体参数以脚本 `help` 与程序 `--help` 为准。

## GPU service 与测试

`gpu/src/rf_gpu_vulkan_backend.c` 提供 Vulkan device、通用 compute framebuffer smoke、Scene graphics/skinning 与 native present。`gpu/src/rf_gpu_graphics_test.c` 覆盖 Scene layers、颜色合批和资源复用；GPU service/probe 测试检查设备能力与初始化边界。CPU 软件 renderer 保持独立构建入口。

旧 GPU Compute Raster pack/bin、SPIR-V、mixed executor 和 bridge 的构建目标已移除。历史构建与诊断说明见[退役归档](../archive/gpu-compute-retirement/README.md)，不能用于当前验收。

修改源文件列表时检查根 `Makefile`、`windows/Makefile`、self 构建规则与 package 资源复制。新增资产同时检查加载和内嵌依赖。
