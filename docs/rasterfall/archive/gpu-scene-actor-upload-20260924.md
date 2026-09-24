# GPU Scene 模块化队员上传优化

> 状态：历史测量现场；不代表正式 FPS 签收
> 日期：2026-09-24
> 当前顺序：[活动计划](../plans/gpu-scene-renderer.md)

## 改动和归因

正式八名模块化队员包含 43 个身体、装备和武器 mesh。原 skin update 每次创建两份临时 staging，
完整复制 bind/palette，再提交 transfer/compute 并同步等待，最后销毁 staging；目标本身可 host-visible 也如此。
新增 `SCENE-ACTOR-COST` 分解 load、CPU pack 和 upload/skin/wait，定位主要开销在最后一段。

现于退休后直接写入 host-visible 目标；不能直接写入时按资源容量保留 staging，随 resource 回收。
非 coherent 内存仍 flush，备用路径仍保留 transfer→compute 同步，蒙皮输出仍同步到 vertex/transfer。
没有修改动画、IK、输入几何、palette、draw 和蒙皮次数；bind/palette 上传字节没有实质下降。
也没有消除逐资源 fence 等待或引入多帧流水。

## 同包三轮 A/B

Windows native、RTX 3050 Laptop GPU、1280×720，固定 near 0/30/60 敌人、固定 tick。
`tools/gpu_scene_cost.ps1 -Experiment ActorUpload -OutputDirectory tmp/actor-upload-ab`：三轮交替
legacy/optimized，每轮 64 帧，排除前 8 帧；每个场景每种模式共 168 个有效帧。
两侧都启用上轮动态资源复用；legacy 只恢复角色临时 staging 上传。
所有轮次逐帧 draw/敌人/剔除/程序角色/补充角色数量序列一致。
包 SHA-256：`4D144E8713314E0572A260338C87A64321E55C834D24C2A0C8C239168CFE3B7D`。

下表使用全部有效帧的平均值，不是三轮中位数；原始日志和单轮分位数见 `tmp/actor-upload-ab/report.json`。

| 敌人数 | 正式队员准备：旧 → 新 | 降幅 | 诊断帧墙钟：旧 → 新 |
| --- | --- | --- | --- |
| 0 | 31.44 → 7.29 ms | 76.8% | 45.90 → 21.29 ms |
| 30 | 34.72 → 7.62 ms | 78.1% | 67.96 → 35.92 ms |
| 60 | 35.35 → 8.75 ms | 75.3% | 89.06 → 55.42 ms |

队员准备内部，CPU pack 新旧均约 1.14–1.19 ms；load 约 0.02–0.03 ms。
upload/skin/wait 从 30.18/33.38/34.04 ms 降至 6.03/6.36/7.44 ms。
其余小差额包含计时行输出、结果复制和外层编排。

## 优化后的平均耗时和累计占比

| 阶段 | 0 敌人 ms / 占比 | 30 敌人 ms / 占比 | 60 敌人 ms / 占比 |
| --- | --- | --- | --- |
| WORLD 准备 | 0.69 / 3.3% | 0.72 / 2.0% | 0.72 / 1.3% |
| 正式模块化队员准备 | 7.29 / 34.2% | 7.62 / 21.2% | 8.75 / 15.8% |
| 敌人及程序角色准备 | 3.92 / 18.4% | 10.24 / 28.5% | 18.41 / 33.2% |
| 天空、特效、武器、HUD 分层准备 | 3.89 / 18.3% | 4.65 / 12.9% | 5.62 / 10.1% |
| 提交与退休等待 | 2.74 / 12.9% | 8.51 / 23.7% | 16.39 / 29.6% |
| 其余未细分 | 2.75 / 12.9% | 4.19 / 11.7% | 5.54 / 10.0% |
| 合计 | 21.29 | 35.92 | 55.42 |

占比为累计分段时间 / 累计帧墙钟；显示舍入可能使分项和略有差异。
这些是串行墙钟阶段。队员 upload/skin/wait 尚未拆成纯 CPU、GPU 和等待；提交退休也不是纯 GPU draw。
GPU timestamp 与墙钟重叠，不另行相加。统计终点仍为 Scene 退休后、日志与循环尾部之前，
不是完整端到端帧时间。每帧诊断输出、设备时钟和短采样仍影响结果，其他阶段的变快不全归因于角色上传。
尚未达到 60 FPS。

## 验证

- 最终 Windows package 与 A/B、下述验证的 executable 哈希一致；保留已有工作区改动。
- `tmp/actor-upload-verify/{legacy,optimized,staging}/`：near 30、程序/补充角色、特效三组各四帧，
  三条上传路径全部通过 validation/sync；验证层加载及 Synchronization 启用均由日志证明。
  第四帧三组 PPM 在三条路径间逐字节一致，哈希见 `pixel-comparison.json`。
- 同目录 `native.out/.err`：120 帧 Scene native fixture 退出 0，涵盖 skin CPU 对照、资源增长、
  resize、world retirement、CPU backing 复用与最终 live/retired/pinned 全零；validation/sync 无错误。
- `pose.out` 和 `logic.out`：完整 Scene pose 专项与逻辑回归通过。
- 文档链接检查和 `git diff --check` 通过。

本轮未重跑完整游戏交互、实机联机、全故障矩阵和长时 soak，也未宣称非 coherent 硬件分支已有实机覆盖。
强制 staging 在当前设备验证备用复制路径，不等同于另一种显存架构的实机签收。
