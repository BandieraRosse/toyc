# Hardware Graphics 开发计划

> 计划更新：2026-09-21
> 当前进度补充：HG-5 的 30/60 敌人 strict-native 基线已在 Windows Intel 完成；60 敌人 whole-loop/GPU Raster 中位数为 58.888/26.547 ms。下一步进入 HG-5A character geometry，GPU skinning 保持 HG-5B 独立迁移。
> 当前进度补充：HG-4A Ground 与 HG-4B map/boundary geometry 已按 Windows Intel 签收：ground 及 wall、box、ramp、opaque platform、boundary 五类地图几何已迁移为 world-generation persistent Draw；专用 V1 runtime fixture、CPU/native 视觉对照、Fog/四 extent resize、world-cycle、Windows package 与完整逻辑回归通过。HG-4 完成；Linux/其他 GPU 未重复实机验收。
> 当前进度：HG-0、HG-1A、HG-1B、HG-2A、HG-2B、HG-2C1--2C5、P0 性能事实门禁、HG-3 与 AI frontend checkpoint 已按 Windows Intel Iris Xe 口径完成。HG-3 的完整 120 帧 near/mid/Fog、320 帧 Campaign、static RMESH 审计及建筑内部/远处薄结构 native capture 均通过；AI frontend 的性能/Campaign audit 与边缘入镜、近面交叉角色 capture 也已通过。Linux/其他 GPU 未验收。
> 实施入口：[架构与基线](rasterfall/docs/hardware-graphics-architecture.md)；[HG-0 checkpoint 证据与限制](rasterfall/docs/hardware-graphics-hg0.md)。

| Checkpoint | 状态 | 交付/下一步 |
| --- | --- | --- |
| HG-0 | 完成 | ownership/Draw V0/数值合同、可复现 Windows 测量脚本、CPU/compute captures、native/Fog/正式地图波次基线 |
| HG-1A | 完成 | CPU planar 前置修复；普通 opaque static RMESH 按实例/submesh 提交 Draw，同步 reference；命令/color/depth 精确回归与 Windows Intel 基线通过 |
| HG-1B | 完成 | CPU bundle registry、generation、Core 帧 pin 与延迟释放；[实现与验证](rasterfall/docs/hardware-graphics-hg1b.md) |
| HG-2A | 完成 | 持久 device-local VB/IB/texels、flat/nearest、RGBA8/D32 离屏 indexed draw；[数值合同与验收边界](rasterfall/docs/hardware-graphics-hg2a.md) |
| HG-2B | 完成（Windows Intel） | [正常 Windows strict native 混合帧](rasterfall/docs/hardware-graphics-hg2b.md)、遮挡/层顺序 fixture、近/中距离窗口帧与四 extent resize 按视觉正常标准通过 |
| HG-2C1–2C5 | 完成（Windows Intel） | [mixed 帧诊断、共享 target、统一 command recording、多 frame slot 与 presenter ownership](rasterfall/docs/hardware-graphics-hg2c.md) |
| P0 性能事实门禁 | 完成（Windows Intel） | 预热后 median/P95、实际帧间隔、审计扰动、历史 GPU timestamp 对齐、Campaign 波次有效性校验；入口为 `tools/hardware_graphics_metrics.ps1` 与更新后的 baseline 脚本 |
| HG-3A / HG-3B | 完成 | [普通 opaque static RMESH 扩围](rasterfall/docs/hardware-graphics-hg3.md)：实现覆盖全部当前 eligible RMESH；连续 Draw 空 flush 已消除，完整 Intel baseline、Campaign 与专项视觉门禁通过 |
| AI frontend checkpoint | 完成 | [角色 frontend 缓存与组合边界](rasterfall/docs/hardware-graphics-ai-frontend.md)：finalized pose/skinning、被动 gear 与 active weapon placement cache 及 body/gear/weapon 组合 bounds 已接入；性能/Campaign audit、边缘入镜与近面交叉专用 capture 通过 |
| HG-4A / HG-4B | 完成（Windows Intel） | [Ground / map geometry](rasterfall/docs/hardware-graphics-hg4.md)的视觉、生命周期与性能审计门禁已完成；动态 air-gate、透明 platform 与 texture wall 按合同保留 RasterCmd 路径 |
| HG-5A / HG-5B | 基线完成，HG-5A 待开发 | [Character geometry / GPU skinning](rasterfall/docs/hardware-graphics-hg5.md)；正式 30/60 敌人 strict-native 基线已通过，按 geometry → skinning 顺序实施 |

## P0 性能事实门禁

P0 不改变 renderer 输出，只修正测量和结论边界：

- 正常固定场景和波次均丢弃前 16 帧，报告 mean、median、P95、maximum；首帧/冷启动单列。
- `whole_loop_ms` 与 `frame_interval_ms` 同时保留。逐帧 `--frame-audit` 写日志发生在 whole-loop 采样之后，
  因此以 `frame_interval[N] - whole_loop[N-1]` 估算审计日志与未采样调度间隙，不能把两者混为 FPS。
- `mixed-gpu frame=N` 是已回收历史帧；GPU timestamp 必须按其自身 frame ID 应用预热窗口。
- `present_wall_ms`、`native_acquire_ms` 与 `mixed_raster_segment_ms` 可能嵌套，不相加；
  `native_present_ms` 才是 present API 调用墙钟，Acquire 等待单独解释为 swapchain/显示背压。
- 正式波次必须显式 `--map rasterfall/assets/maps/rasterfall.map`，并同时通过 `world=1`、存在敌人命令和
  `GPU-WAVE-REPRO` 活敌输出门禁。默认 Outpost 的 320 帧只能称为低负载样本。
- 每个关键场景补一轮关闭逐帧审计的 FPS/运行时对照，避免把日志成本当成 renderer 成本。

固定重场景当前指向两个主成本：static/world scene CPU 提交与 GPU compute Raster；低负载样本中的
Acquire 等待不是优先优化对象。因而 P0 完成后的默认顺序为 HG-3 → AI frontend checkpoint → HG-4 → HG-5，
bridge、Post、overlay 和 present API 微优化暂不前置。

P0 实机结果保存在 `tmp/p0-gpu-20260920-182809/`（生成物不提交）。固定 near/0 第 17--120 帧
whole-loop 中位数/P95 为 29.956/32.975 ms，scene 为 13.881/15.686 ms，AI 提交为
6.283/6.760 ms；GPU Raster 为 15.608/31.452 ms。正式 Campaign 波次第 17--320 帧为
320/320 native、279 帧含敌人命令，whole-loop 中位数/P95 60.847/71.792 ms，scene
34.950/42.834 ms，GPU Raster 19.691/27.894 ms；native acquire 中位数 0.005 ms、present API
0.021 ms、hot queue-idle 为零。逐帧审计与未采样调度间隙中位数约 8.47 ms；Campaign 有审计/无审计
两次进程墙钟为 25.40/21.36 秒，进一步确认审计会扰动观测 FPS。后续 HG-3 以正式 Campaign 与固定
near/0 两类负载共同验收，不能再用默认 Outpost 320 帧替代波次。

HG-0 校正：显式 frame audit 现逐帧输出；normal near/mid 标签只控制初始相机，稳定位置以实际审计为准；
波次必须显式加载 Campaign 并验证活敌。原始 HEAD 的独立 texture full-scan fixture 已复现失败，
HG-1A 已定位为 CPU planar vertex-lit 忽略 alpha/no-depth-write，并补齐恒定与插值光照路径；
前置记录见 [HG-1A 前置修复](rasterfall/docs/hardware-graphics-hg1-preflight.md)，Draw 接入及验收见
[HG-1A Draw/reference](rasterfall/docs/hardware-graphics-hg1a.md)。Windows strict native 已启用 hardware normal-frame 接入；其他平台/模式仍按各自现有路径运行。

原始调研与阶段实施草案见 [归档](rasterfall/docs/archive/hardware-graphics-original-plan-2026-09-19.md)；当前实现与验收以各 checkpoint 文档和 CLI 输出为准。
