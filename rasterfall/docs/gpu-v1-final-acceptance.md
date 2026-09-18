# GPU V1 最终收尾与冻结验收

> 文档更新：2026-09-18
> 源码核对基线：GPU Required Runtime Contract 已实现；GPU-8B2 local gate 待本轮回归，GPU-8B1 / GPU-9A 仍须 Windows Intel 实机签收，尚未冻结。

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
| Host/client | local viewmodel、remote actors/weapons/effects、HUD | join、长期移动/战斗、disconnect/rejoin |
| Soak | Campaign 与 Outpost 往返、全部常见 producer | Intel 实机长期运行并正常 shutdown |

## Checkpoint 与冻结条件

1. **C0 Runtime strict（代码完成）：** required 初始化与逐帧 fatal contract；CLI 拒绝非 native strict 组合。
2. **C1 Local regression：** Linux build、logic-test、现有 retained/transparent/Post fixture、参数负例通过。
3. **C2 Windows build：** MinGW 构建通过；不以 WSL software Vulkan 代替 Intel 验收。
4. **C3 Intel functional：** 上表 Outpost/Campaign/network 与 resize/minimize/restore 全通过。
5. **C4 Intel soak/transfer：** Fog 与长期 gameplay 通过，所有 frame audit 均零 fallback/readback/copy；
   native present failure 注入必须非零退出。
6. **C5 Freeze：** C0--C4 证据齐全后，同一 checkpoint 将 GPU-8B1、GPU-8B2、GPU-9A 标为 FROZEN。

当前不得提前宣称 C5。实机失败只修正常 gameplay 触发的 coverage 或 lifecycle 缺口，不增加 legacy/
diagnostic feature。

## GPU V1 冻结后的演进边界

GPU V2 不在本轮实现。后续应以 Semantic Render IR 替代逐步膨胀的低层 Raster ABI，并引入 persistent
GPU resources、GPU geometry ownership，最终组合 hardware raster 与 compute；这些变化必须另立版本和
迁移计划，不回填进已冻结的 GPU V1。
