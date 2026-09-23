# Rasterfall Windows 构建入口

在仓库根目录使用 Windows 原生 PowerShell：

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

Windows 构建与 package 位于 `build-windows/`。环境安装、命令参数、进程等待、日志和物理 GPU 验收流程见[Windows Native 指南](../docs/rasterfall/guides/windows-native.md)；跨平台构建边界见[构建与平台](../docs/rasterfall/guides/build-platforms.md)。项目用户入口见[Rasterfall README](../rasterfall/README.md)。

本目录保存 Windows 平台实现、`NativeCodex.ps1` 和 `windows/Makefile`。平台状态与接口所有权见[运行时架构](../docs/rasterfall/architecture/runtime.md)和[GPU 渲染架构](../docs/rasterfall/architecture/gpu-rendering-architecture.md)。
