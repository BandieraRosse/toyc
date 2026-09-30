# Rasterfall 计划入口

> 状态：当前
> 所有者：Rasterfall 项目优先级
> 决策日期：2026-09-29

## Active plan

当前唯一活动计划为[RF 新一代角色体系与 GPU Scene 接入](private-anime-character-gpu.md)。交付通用角色资产、动画、材质与 GPU 资源体系，以 RF-C01 为首个正式内容包，以前哨站 `RF_MODEL_LAB` 为首个正常帧消费者；后续角色通过规范内容和目录接入。旧 CPU 动漫绘制和联机 Scene 暂不进入本计划。

执行顺序为 P0 合同与能力矩阵 → P1 资产构建与 GPU 资源 → P2 材质纹理 → P4 实验场验收 → P5 正常单人复用；P3 模型、UV、纹理、动作与 LOD 在 P0 后并行制作，并在 P4 汇合。实验场交付与单人默认 GPU 切换分开签收。当前各阶段均未完成；P0 已开始，已落地[角色包与材质合同草案](../reference/character-package-v1.md)、GLB 能力审计和新材质元数据校验。继续补齐最小变形/材质视觉证据并冻结二进制扩展布局，不先制作临时角色绘制旁路。

角色路线已选择以 A 为起点的成熟动漫方向；内容造型审阅与通用基础设施推进可以并行。
用户随后要求优先看到实验场 GPU 模型，已将现有 v14 的目录 body 预览与通用分块提交提前落地；
这是实验场中间交付，不代表 P0–P5 完成。复现与限制见 [Scene 工作流](../guides/gpu-scene-fixture.md#rf_model_lab-角色预览)。
当前先审阅 V22h 的底发与侧后发连续塑形；两侧灰模、剪影和原材质对照检查叶状边界、底发下缘及整体发量。候选与限制见活动计划及其链接的 RF-C01 设计稿，不以离线样本代表阶段完成。
后续造型迭代显式选择 v022h 候选清单，继续使用 V20c 锁定的空间接口，按[分部件工作流](../guides/character-parts.md)限定修改范围；V07 初始组装与 V08–V21 对照保留。持枪、动作与游戏 GPU 材质进入本计划交付范围；随视角修脸继续延期。

原[CPU 与 GPU Scene 默认渲染统一计划](../archive/rendering-baseline-unification-20260927.md)已归档；其未完成的双后端画面对照不是当前角色接入门槛。共同功能清点仍由[渲染 baseline](../reference/rendering-baseline.md)保存，不自动成为本计划待办。GPU Compute Raster 退役记录见[归档](../archive/gpu-compute-retirement/README.md)。

用户确认独立 Scene 阶段收尾：旧地图实机游玩已流畅，性能达到本阶段预期。此结论是本次实机使用判断；历史诊断与正式五轮性能标准的测量范围保持原样，不补写未执行的签收数据。

原[独立 GPU Scene 渲染架构计划](../archive/gpu-scene-renderer.md)及其阶段草案已归档。Scene 的当前运行边界见[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)，复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)。

## 延期事项

- Outpost V1 扩建：渲染路线退役已完成，后续按新计划继续；阶段记录见 `outpost-v1.md`。
- 敌人槽位复用：降低大批敌人生成时的冷启动资源分配与准备负载；暂不作为本阶段收尾条件。
- 其余小幅性能优化按后续实测需要再立项，不继续沿用归档计划中的阶段 4/5 执行链。

归档中的未完成清单只代表当时的计划，不自动成为当前待办。
