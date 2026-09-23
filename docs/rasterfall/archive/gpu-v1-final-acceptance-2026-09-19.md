# GPU V1 最终收尾与冻结验收

> 状态：历史
> 归档原因：阶段记录，当前设计与执行顺序已转入维护入口
> 当前入口：[Rasterfall 维护者入口](../README.md)

> 历史归档：2026-09-19 功能阶段结束后的验收现场；当前边界以 [GPU 渲染架构](../gpu-rendering-architecture.md) 和 [GPU 验收与诊断](../guides/gpu-validation.md) 为准。

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 `rf_core_host.c` retained WORLD partition 同步实际分配容量；跨帧缩小/增长回归覆盖缓存复用。
> 源码核对基线补充：2026-09-19 老地图右转故障定位为 Texture V1 pack 重复回扫历史 command 导致 200ms watchdog；本帧唯一纹理视图表修复后，strict GPU 全向扫视可越过同一高负载姿态。
> 源码核对基线：2026-09-19 Intel Iris Xe、正式地图 320 帧 strict native `--gpu-wave-repro --frame-audit` 日志；320/320 帧 `gpu-native`，零 fallback、无效层切换、readback 和 CPU framebuffer copy。用户确认正常游玩核心功能与窗口拉伸。成功路径逐帧 pack 日志已移除，失败诊断与 frame audit 保留。

## 本轮验收状态

本轮 GPU V1 本地功能收尾已签收：Windows Intel strict native、Fog/Post smoke、正式地图 320 帧波次复现、核心游玩和窗口拉伸均已确认。320 帧日志中的 WORLD、TRANSPARENT、EFFECTS、VIEWMODEL 均有命令，最终路径始终为 `gpu-native`。当前帧率偏低，用户决定不以长期运行作为本轮收尾条件。此结论是本地功能验收，不是性能达标或完整生命周期矩阵通过；长期 soak、反复 pause/resume、minimize/restore、world switch、死亡/重生及 native present 失败注入没有同一份完整实机证据。Host/client 联机不在本轮范围。

## Frozen Normal Gameplay 边界

GPU V1 只承诺正常 gameplay 实际提交的 RenderFrame V1：SKY、WORLD opaque、WORLD transparent、
EFFECTS、VIEWMODEL 和 CPU 生成/GPU composite 的 OVERLAY。Campaign 与 Outpost 的地图、静态物件、
actors、network actors、普通/特殊 enemies、world/viewmodel weapons、muzzle/tracer/impact/death effects、
透明 platform/air-gate/dissolve/dust、HUD 和 Fog V0 属于冻结候选集合。

Legacy anime/toon、Console/Desktop、模型 gallery、旧 PMX/VMD、visual capture 和其他 diagnostic fixture
不属于 normal contract。它们不得推动 Raster V1 ABI 扩张；若未来进入正常 gameplay，必须重新开启
GPU V1 contract 评审或进入 GPU V2。

## `--gpu-required` runtime contract

严格运行必须同时指定：

```text
--renderer gpu-compute --gpu-native-present --gpu-required
```

它要求初始化、每个正常帧和 shutdown 前的最后一帧始终保持 GPU native 路径。以下任一情况立即以
非零状态退出，不允许 CPU replay、software present 或永久降级：unsupported command/material/texture/
edge/overlay、WORLD 到 VIEWMODEL 的 direct pixel、无效 layer transition、retained consumer/resize 失败、
GPU Post 请求失败、native present 失败，以及 native timing 报告非零 color readback 或 CPU framebuffer copy。
`GPU_OPTIONAL` 保留开发兼容回退语义。

## Zero-fallback 验收矩阵

每行都使用 strict 参数与 `--frame-audit`；持续阶段至少覆盖一次完整交战、死亡/重生、world switch，
网络行覆盖 host 与 client 的 remote actor 加入、移动、射击和离开。每帧共同门禁为
`path=gpu-native`、`pre_post_cpu_fallback=0`、`fallback_reason=0`、`invalid_transitions=0`、
`color-readback=0`、`cpu-framebuffer-copy=0`，进程正常退出。

| 场景 | 必须观察的 normal coverage | 生命周期 |
| --- | --- | --- |
| Outpost | sky/map/static props、actors、weapons、透明物、HUD | 启动、resize、minimize/restore、切入 Campaign |
| Campaign idle/motion | actors、network actors、普通/特殊 enemies、world weapons | resize、多次 pause/resume、死亡/重生 |
| Campaign combat | viewmodel weapon/hands、muzzle、tracer、impact、death fragment/dust/dissolve | reload、换枪、连续波次 |
| Campaign Fog | 上述完整集合及 viewmodel coverage mask | Fog 开/关各一轮、resize 后保持 |
| Host/client | 本轮不纳入 GPU V1 本地验收；联机协议另按 networking 文档维护 | 不作为本轮 C3/C4 通过条件 |
| Soak | Campaign 与 Outpost 往返、全部常见 producer | Intel 实机长期运行并正常 shutdown |

## Checkpoint 与冻结条件

本轮进度：C0--C2 完成；C3 的本地玩法、strict native、窗口拉伸和 320 帧波次复现已签收；C4 的 Fog/Post smoke 已完成。C5 按用户确认的**本地功能范围**收尾，未覆盖项如下保留为后续验证，不推定为已通过。

1. **C0 Runtime strict（代码完成）：** required 初始化与逐帧 fatal contract；CLI 拒绝非 native strict 组合。
2. **C1 Local regression：** Linux build、logic-test、现有 retained/transparent/Post fixture、参数负例通过。
3. **C2 Windows build：** MinGW 构建通过；不以 WSL software Vulkan 代替 Intel 验收。
4. **C3 Intel functional（本地范围通过）：** 核心玩法、窗口拉伸、正式地图 320 帧 strict native 波次复现通过；pause/resume、minimize/restore、world switch 和死亡/重生的组合矩阵未签收。
5. **C4 Intel soak/transfer（部分通过）：** Fog/Post 120 帧 smoke 与零 fallback/readback/copy 已确认；长期 soak 因当前低帧率延期，native present 失败注入未执行。
6. **C5 本轮收尾（本地功能范围完成）：** GPU V1 的功能边界固定，后续只修复实际发现的问题。GPU-8B1、GPU-8B2、GPU-9A 不标记为完整矩阵 `FROZEN`；性能与上述未覆盖项进入后续验证。

不要将本轮收尾表述为性能达标或完整矩阵冻结。实机失败只修正常 gameplay 触发的 coverage 或 lifecycle 缺口，不增加 legacy/diagnostic feature。

## GPU V1 冻结后的演进边界

GPU V2 不在本轮实现。后续应以 Semantic Render IR 替代逐步膨胀的低层 Raster ABI，并引入 persistent
GPU resources、GPU geometry ownership，最终组合 hardware raster 与 compute；这些变化必须另立版本和
迁移计划，不回填进已冻结的 GPU V1。
