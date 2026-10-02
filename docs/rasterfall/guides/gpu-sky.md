# GPU 天空开发与验证

> 状态：当前操作指南
> 所有者：Rasterfall GPU Scene

架构与功能边界见[GPU 天空](../architecture/gpu-sky.md)。正常 GPU 游戏入口自动使用新天空，
CPU 使用旧天空。先完成 Windows native build，然后启动前哨站天空视角：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 build
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_sky_study.ps1
```

脚本支持 clear（默认清透晴空）、rain（雨后）、warm（暖光）预设；可选择四个方向、天顶、
俯视和默认晴空太阳方向。参数以脚本 `param` 和程序 `--help` 为准。
交互运行允许自由转头、移动；`-Capture` 使用固定时间，等待真实进程结束并检查日志与生成物。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_sky_study.ps1 -Preset clear -View north -Capture -OutputDirectory tmp/sky-review
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_sky_study.ps1 -Preset rain -View north -Capture -OutputDirectory tmp/sky-review
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_sky_study.ps1 -Preset warm -View north -Capture -OutputDirectory tmp/sky-review
```

输出为 `.bmp.scene.ppm`，旁边 `.out` / `.err` 保留 native 审计。重复同一视角应换新证据目录，
不要覆盖旧结果。默认时间为零，`-Time` 可选择另一冻结时刻。概念图只用于美术比较，不进入 package。

## 质量与诊断

进程环境 `RF_GPU_SKY_SCALE` 选择每轴下采样倍率：4 为默认，2 为较高分辨率，1 为更密集步进的全分辨率参考；
需要重新启动进程。`RF_GPU_SKY_PRESET` 选择气氛，`RF_GPU_SKY_TIME` 冻结秒数。脚本临时设置
预设/时间并在退出时恢复，不修改系统环境。

`rasterfall.exe --gpu-lighting-test` 同时检查天空确定性、平移独立、旋转响应、共享太阳、零深度写入、
不透明遮挡、天顶/下半球、resize 和非法参数。测试包含显式 readback，不能代替 native present。

设置 `RF_GPU_SKY_BENCH=1` 后运行该测试，会额外采集 1920×1080 天空与纯色 HDR 背景的 GPU
timestamp 对照：预热后交替采集纯色/天空配对帧，并轮换先后顺序，输出 p50/p95、首帧 GPU 时间和
配对差值的中位增量，减小频率与温度漂移的影响。它测量隔离天空成本，排除 capture
readback；不是前哨站完整帧基线，也不承诺首帧 CPU pipeline 创建成本。分别启动不同质量档作比较。
`SKY-COMPUTE` 单独报告天空 compute 时间戳的 p50/p95，排除预热中的噪声烘焙、后续 HDR 合成及读回；
它包含在整段 GPU 时间内，不能再次相加。配对增量仍保留，用于观察合成等额外成本。

着色器改动后使用 `tools/generate_gpu_graphics_spirv.py <glslangValidator路径>` 更新内嵌 SPIR-V，
再构建 Windows。普通运行或 package 不需要 Vulkan SDK 或 shader 编译器。

## 实机审阅

遵循[Windows native](windows-native.md)的进程、日志与退出码规则。四向、天顶和俯视捕获检查接缝、
云层断裂和下半球；连续转头与慢走检查运动稳定性；太阳方向与背光比较云体厚度。建筑屋檐、透明
实验牌、细杆、枪械和 HUD 用于检查分层与轮廓。最终美术判断需在正常游玩中与角色、敌人同框进行。

独立 compute 资源的提交、resize、暂停/恢复与地图切换复用
`tools/gpu_scene_play.ps1 -Stage Interactive` / `-Stage World`；Vulkan validation 需使用实际可用的
layer 并检查完整日志。验收门槛见[设计研究](../reference/sky-v2-design-study.md#实机验收条件)。
