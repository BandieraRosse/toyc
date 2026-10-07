# 角色加速公共路径首轮

> 状态：历史现场（本轮实现与限定验证完成）
> 日期：2026-10-07
> 归档原因：保留一次实现、对照和验证边界，不替代当前架构。
> 当前所有权：[统一角色加速](../architecture/gpu-rendering-architecture.md#统一角色加速)、[动画架构](../architecture/animation-architecture.md)

本轮把 Block 与六种普通感染体接入共享固定网格模板、实例 palette、上传和主／AUX 消费机制。
Humanoid 共用 GPU 更新事务，并增加 owner-local 不可变模型池，避免每个 GPU 实例重复加载同一路径。
普通感染体不再逐帧 CPU 蒙皮／展开几何；原轮廓颜色乘数迁到 GPU 顶点阶段，保持几何法线光照。
没有新增玩法状态、动画降频或并行求值。特感 rigid producer、跨实例 GPU bind 去重和跨 adapter
目录整合仍未完成，不以公共入口已建立代表这些工作已完成。

## 验证

- Windows 原生最终构建与完整 `NativeCodex.ps1 test` 退出 0；新增资源池回归覆盖共享 backing、
  独立代际、pin 退休、仍被引用时拒绝销毁和失败加载。见[最终逻辑日志](../../../tmp/character-final-logic.log)。
- 公开姿态套件通过 Block 720 组、感染体 882 组，最大位置误差均为 2 RFU；覆盖全部步态取值、
  bind、死亡压缩／翻滚、失败传播及输出事务。见[公开姿态日志](../../../tmp/character-pose.log)。
- NVIDIA 原生 GPU 回归通过 7183 checks，含 mode 4 与 CPU 颜色乘数的颜色／深度对照、蒙皮
  取消、共享 reader、当前姿态 bounds、主／AUX／阴影及纹理生命周期。见[GPU 日志](../../../tmp/character-gpu-test-final.log)。
- Khronos 与 synchronization validation 确认加载。主／辅助、双辅助及强制 staging 分别完成
  8、4、8 帧后正常关闭，五种 present 故障退出符合合同，无 VUID 或 SYNC-HAZARD。
  见[初轮生命周期报告](../../../tmp/character-lifecycle/report.json)。该轮在最后加入 Humanoid 模型池之前。
- 关闭验证层的持续 AUX 检查各采样 121 帧：单位镜头刷新 26 次，双镜头分别刷新 29／28 次，
  `UI-PERF valid=1`；没有同帧双槽刷新样本。见[持续 AUX 报告](../../../tmp/character-aux-continuous/report.json)。
- 加入模型池后的原生换图通过 120 帧，覆盖三次 world generation 切换。见[换图日志](../../../tmp/character-world-final/world-cycle.out)。
- 最终模型池版本补跑带验证层的 60 敌人／AUX（7 帧正常关闭）与第 2 帧 submit-failure，
  退出符合合同、无同步／验证错误，见[最终生命周期报告](../../../tmp/character-lifecycle-final/report.json)。

完整私有姿态组曾在 RF-C01 模型加载处失败；本机缺少对应私有资产，未计为通过。
公开套件使用 `RF_GPU_POSE_PUBLIC_TEST=1`，不以替代模型掩盖该缺口。

同日后续已从旧 V22h 基线和历史制作脚本重建 V028，完整私有姿态组重新运行通过，
包括 grip 594 组约束、120 组动作采样和全部枪械 810 组握持检查。
资产恢复来源、实机捕获及重建二进制差异见 [V028 恢复记录](rf-c01-v028-recovery-20261007.md)。

## 成本与画面

60 敌人固定 tick 诊断首帧构建 14 份模板、共享 64 次；后续帧 bind 上传为零，
60 份感染体局部姿态命中缓存。见[来源成本日志](../../../tmp/character-retained-form/near.out)。
这些计数证明避免了重复准备，不代表正常玩法帧率。

同包 NVIDIA、1920×1080、每轮 120 个稳态样本，三轮交替关闭／开启感染体常驻路径。
该对照在最终加入 Humanoid 模型池之前完成，只评价感染体迁移，不测模型池加载收益。
程序／资产哈希保持一致；见[对照报告及完整分布](../../../tmp/character-perf/report.json)。

| 轮次 | 平均整帧 ms，旧→新 | P95 整帧 ms，旧→新 | 敌人 CPU 准备 ms，旧→新 |
| --- | --- | --- | --- |
| 1 | 12.377→12.330 | 16.340→15.037 | 2.201→2.066 |
| 2 | 11.848→12.480 | 14.704→14.536 | 2.110→2.118 |
| 3 | 12.706→12.000 | 16.217→13.981 | 2.311→2.020 |

平均整帧并非逐轮改善，不能据此声称稳定 FPS 提升。旧路径也能缓存不变的动态几何；
后续应对真实移动感染体、不同数量和实际显存驻留继续测量，而非外推轻负载收益。

固定 tick 成对图检发现最初移除 CPU form-light 会明显提亮身体，已把该计算迁到 GPU 修正。
修正后对照构图和姿态对应，仍有轮廓、着色与阴影差异：整图 RGB 平均绝对差约 2.113/255，
变化像素约 26.87%，不能宣称逐像素一致。见[成对画面](../../../tmp/character-compare-form.png)。
常驻 GPU bind／输出逐实例持有，增加的显存尚未实测；CPU 模板共享不等于显存去重。
