# GPU Scene 阶段 0：迁移基线状态

> 状态：基线采集中；本页不构成阶段 0 退出签收
>
> 核对日期：2026-09-23

目标场景和门槛见[活动计划](gpu-scene-renderer.md)与[GPU 性能标准](../reference/gpu-performance-standards.md)。
本页只记录可复核的采样身份和未补齐项；旧路径的正式性能判断仍须按
[GPU 验收指南](../guides/gpu-validation.md)使用低扰动五轮 A/B。

## 本次 package 与一轮结构审计

Windows native `doctor`、`build`、`package` 通过。以 package 内 `rasterfall.exe` 运行
`tools/gpu_rb0_sampling.ps1 -Rounds 1`，结果 PASS；原始 manifest、四份 runtime log、metrics 和
summary 保存在本地 `tmp/gpu-rb0-sampling-20260923-195003/`（生成物不提交）。

| 固定身份 | 值 |
| --- | --- |
| Git HEAD | `c516ce0b5ab0bf51a8c442db76951e39eecf954e`；工作区另有未提交文档变化，见 manifest |
| package executable SHA-256 | `B05B39386B43738E470A115B6DA613D3D39BA4E4306378B99879476772F9A3FD` |
| GPU | runtime 报告 `NVIDIA GeForce RTX 3050 Laptop GPU`；系统 CIM 驱动枚举拒绝访问，驱动版本尚未固定 |
| extent / 电源 | 1280×720；交流电；采样时电源方案 GUID `85d583c5-cf2e-4197-80fd-3789a227a72c` |
| 命令 / 模式 | `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 1`；逐帧 audit |

| 场景 | 帧数 | workload sequence SHA-256 | whole-loop median / P95（ms） | bridge transfers / bytes |
| --- | ---: | --- | ---: | ---: |
| near 0 | 120 | `0734ec6bdccdb9e72e962a34a2791f370e0c8d9a733d556a302b8d02b60e5a60` | 25.996 / 39.656 | 4 / 29,491,200 |
| near 30 | 120 | `d5947682ad10489737f1d10a41d9d0a19cee0ebefbba5c8755eb968e683b5faf` | 42.107 / 332.995 | 4 / 29,491,200 |
| near 60 | 120 | `ddfc7ab8958f2b16930a84d001274db63cc9fb69712535bb9dddc127e913bd1a` | 85.863 / 168.485 | 4 / 29,491,200 |
| Campaign | 320 | `1d5eb114364e826d465adc8d209b3cd381e4caea4e87b53ec1523ce14344bc4f` | 38.201 / 226.893 | 10 / 73,728,000 |

这些时间是**一轮带审计的结构数据**，有明显尾部扰动，不用于 FPS 收益或跨 package 比较。
四场景 metrics 的 `path=gpu-native`；每个场景的 bridge 数在该次采样帧内保持一致。
历史 M2 低扰动五轮数值保留在[活动计划](gpu-scene-renderer.md)，两组采样不可直接相减。
该次 manifest 只含 executable 哈希；采样脚本现已增加整包内容哈希及结束复核，须由后续采样生成，
不能追填到上述已有证据。

## 基线冻结尚缺

1. 在相同 package 上补正式五轮低扰动 near 0/30/60、Campaign 与可核对的原始记录；
   driver library/version、package 内容 hash、构建参数和电源方案一并固化。
2. mid、thin-far、0/30/60 敌人的 required-native 日志，以及固定地图/角色 capture、像素和深度
   reference。当前四场景审计不足以覆盖[画面覆盖矩阵](gpu-scene-coverage.md)。
3. 对固定 capture 审批新硬件路径画面合同：精确项目、数值容差、人工审阅截图和审批记录；
   详见待完成的阶段 0 第四步。
