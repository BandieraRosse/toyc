# Static World Lighting V2 冻结记录

> 状态：历史
> 归档原因：2026-09-21 阶段签收与平台覆盖快照
> 当前入口：[架构](../architecture/static-world-lighting.md)、[验证指南](../guides/static-world-lighting.md)

旧 Phase A/B/C1/C2/C3/D 文档在 V2 冻结时收敛。源码核对基线为 `208532c`，当时核对 `rasterfall_world_light.h/.c`、Runtime Map 输入、normal renderer consumer、诊断 scope 与 Linux/Windows 构建边界。

当时冻结证据覆盖 Linux headless capture、Linux GCC freestanding build 和 Windows MinGW build；Windows runtime、native-present、真实窗口移动中的 flicker/grid snapping 尚需在对应平台任务中单独验收。无窗口结果不能替代物理 Windows GPU/runtime 结论。

当时 Campaign world 约为 153.125 m × 129.6875 m，64×48 field 的 sample 间隔约为 2.43 m × 2.76 m。这是该阶段地图尺寸下的测量，不是光照 field 的固定间距。
