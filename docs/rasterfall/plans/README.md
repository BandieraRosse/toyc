# Rasterfall 计划入口

> 状态：当前
> 所有者：Rasterfall 项目优先级
> 决策日期：2026-09-28

## Active plan

当前唯一活动计划为[RF 私有动漫角色与 GPU Scene 接入](private-anime-character-gpu.md)。目标是创作原创 RF 骨架角色，在 Windows 单人 Scene 完成 GPU 材质、蒙皮、LOD 与正常帧接入，验收后将 GPU Scene 设为单人默认入口。旧 CPU 动漫渲染和联机 Scene 暂不进入本计划。

角色路线已选择以 A 为起点的成熟动漫方向；先联合审阅头部形体与简单离线材质，再推进绑定和动作。
当前先审阅 V07 短发衔接及沿用的 V06 脸部与离线表情；候选与限制见活动计划及其链接的 RF-C01 设计稿，不以静态样本代表阶段完成。
后续造型迭代从 V07 的独立部件基线继续，按[分部件工作流](../guides/character-parts.md)限定脸部、头发和表情的修改范围。

原[CPU 与 GPU Scene 默认渲染统一计划](../archive/rendering-baseline-unification-20260927.md)已归档；其未完成的双后端画面对照不是当前角色接入门槛。共同功能清点仍由[渲染 baseline](../reference/rendering-baseline.md)保存，不自动成为本计划待办。GPU Compute Raster 退役记录见[归档](../archive/gpu-compute-retirement/README.md)。

用户确认独立 Scene 阶段收尾：旧地图实机游玩已流畅，性能达到本阶段预期。此结论是本次实机使用判断；历史诊断与正式五轮性能标准的测量范围保持原样，不补写未执行的签收数据。

原[独立 GPU Scene 渲染架构计划](../archive/gpu-scene-renderer.md)及其阶段草案已归档。Scene 的当前运行边界见[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)，复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)。

## 延期事项

- Outpost V1 扩建：渲染路线退役已完成，后续按新计划继续；阶段记录见 `outpost-v1.md`。
- 敌人槽位复用：降低大批敌人生成时的冷启动资源分配与准备负载；暂不作为本阶段收尾条件。
- 其余小幅性能优化按后续实测需要再立项，不继续沿用归档计划中的阶段 4/5 执行链。

归档中的未完成清单只代表当时的计划，不自动成为当前待办。
