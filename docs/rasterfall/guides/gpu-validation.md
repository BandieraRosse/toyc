# GPU 验收与诊断

> 状态：当前
> 所有者：Windows 原生 GPU 验收
> 最近核对：2026-09-26

当前硬件路径是独立 Scene。GPU Compute Raster 的 Quick/Full、RB0、mixed 和 bridge 指标流程已退役，历史命令与判断见[退役归档](../archive/gpu-compute-retirement/README.md)。

## 日常闭环

在仓库根目录的原生 PowerShell 中运行：

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
```

`test` 在 package 中运行 CPU 逻辑回归；`gpu-test` 在真实 Vulkan 窗口中以 `--renderer gpu-scene --gpu-normal-scene near 0 --frame-audit --frames 120` 提交硬件 Scene。检查进程退出码、日志中的帧数和 `SCENE-SOURCE`、`SCENE-NATIVE` 审计；旧命令、mixed draw、bridge、常规读回必须为零。不能只凭启动成功或单张截图签收。

## 场景和生命周期

`tools/gpu_scene_play.ps1` 提供互动、combat、resize、world cycle、故障注入和长帧场景；具体参数以脚本 `-?` 与 [Scene 工作流](gpu-scene-fixture.md)为准。对窗口进程按[Windows Native](windows-native.md)的等待与日志规则检查真实退出码。GPU 资源、present 和同步的专项回归可运行 `gpu-graphics-test`、Scene source 测试及对应离屏诊断。

完整 package 验收入口：

```powershell
.\windows\NativeCodex.ps1 acceptance
```

该入口执行 120 帧 Scene smoke、normal-frame audit BMP 和确定性视觉 capture。性能判断使用[GPU 性能标准](../reference/gpu-performance-standards.md)及当前 Scene 成本日志，不沿用历史 mixed baseline。
