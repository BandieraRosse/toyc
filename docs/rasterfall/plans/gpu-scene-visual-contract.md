# GPU Scene 阶段 0：画面差异合同草案

> 状态：画面修复参考；2026-09-24 用户决定数值容差与旧画面一致性审批延期至重构后，不阻塞阶段 3
>
> 核对日期：2026-09-23

本合同用于[活动计划](gpu-scene-renderer.md)的新硬件光栅路径。旧 mixed renderer 和 CPU
reference 均保留为独立对照。对比必须使用相同 package 资源、地图、固定 tick、camera、extent、
actor/effect 输入与目标帧；记录两条路径的 executable hash。不能用单张“看起来接近”的截图
代替差分，也不能将旧整数 Raster 的逐像素舍入写成新路径永久 ABI。

## 必须精确的项目

| 项目 | 判定证据 |
| --- | --- |
| 画面内容清单 | 同一固定帧的 map/ground/props、actor body/gear/weapon、projectile、文字、effect、viewmodel、HUD/UI 均出现；实例身份、数量、可见开关和稳定提交顺序一致 |
| 层序与 target | SKY → WORLD opaque → WORLD transparent → EFFECTS → VIEWMODEL → POST 边界 → OVERLAY；VIEWMODEL 独立 depth/coverage，正常 fog-free；无非法层转换 |
| 深度与混合规则 | opaque 测试并写深度；透明 source-over、测试但不写深度，按冻结 ordinal 提交；alpha 零/满、遮挡前后关系及 coverage 规则一致 |
| 来源与资源 | world-light 查询来源、角色材质 floor/颜色覆盖、finalized pose/socket/attachment、handle/generation 和 pin 生命周期一致；缺资源、unsupported、执行失败使 required 整帧失败 |
| 运行边界 | required-native 零 fallback、readback、CPU framebuffer copy、热路径 queue-idle 和半帧 replay；固定 workload sequence hash 一致 |

这些是语义精确，不要求每个颜色字节或深度整数与旧 Raster 相同。对于固定 CPU reference
fixture，输入结构、actor/instance ID、材质参数、变换、三角形可见性和层 ordinal 应可逐项比较；
任何项目缺失先判失败，不能进入图像容差统计。

## 允许有界差异的项目

硬件深度量化、三角形边缘 coverage、纹理采样舍入、插值和光照最后一位可能与整数 Raster 不同。
每个固定 capture 同时保存原始 BMP、深度/coverage 导出或 hash、像素差图和以下统计：
变化像素数与占比、RGB 每通道绝对差的 median/P95/P99/max、边缘带内外差异、深度遮挡
不一致数。边缘带从 reference 几何/深度 discontinuity 生成，不能事后按差图扩大以掩盖错误。

输入、语义和统计方法先冻结；数值容差在首份新路径候选**正式验收前**随固定 capture 审批，按材质族与画面区域给出上限；
允许隔离原型运行并提供原始差异证据，用于确定容差；不能扩大边缘带或阈值掩盖语义错误。
当前不凭空填写统一 RGB/深度阈值。未批准阈值时，差分只能报告原始统计，不能标记 PASS。
显著轮廓位移、消失/新增物体、透明顺序改变、错误的近裁剪或附件 placement，即使整体
像素差占比很小，也判失败。

## 固定审阅集合与记录

| 集合 | 要检查的风险 | 现有入口 |
| --- | --- | --- |
| near 0/30/60、Campaign 目标帧 | 正常帧层序、密度、effect 与 overlay | GPU normal fixed tick + `--gpu-frame-capture`、frame audit；CPU/reference 对应固定输入 |
| mid、thin-far | 远近可见性、LOD、细几何/边缘 | GPU Full/normal scene capture |
| 地图透明与 air gate | 平台/box alpha、depth-write、条件开关 | `rasterfall_render_map_transparency_logic_test` 与固定地图 capture |
| 专用渲染地图 | opaque、LABEL/SIGN、air gate、近处 box 与远处薄墙 | [fixture 工作流](../guides/gpu-scene-fixture.md)；多镜头与开关状态分别冻结 |
| 角色 world near/mid/far、edge-entry、near-crossing | 角色材质、pose、附件、近裁剪 | `--character-world-capture` 与角色 vertex diff |
| 敌人 0/30/60、普通/特殊家族、死亡 effect | body/gear/weapon、透明碎片与尘埃 | normal enemy workload、`--enemy-visual-capture` |
| VIEWMODEL、HUD、world text/status | 独立 depth/coverage、文字位置、UI 合成 | normal native capture、相关 fixture 与 frame audit |

每个审批记录应列出 fixture、命令、输入资源与 hash、package/executable hash、目标帧、
reference/candidate 原图路径、差图和统计、批准的逐项阈值、人工审阅结论与审阅人/日期。
原图和日志保存在 `tmp/` 的独立证据目录，不提交生成物；合同中记录其 manifest 身份和
可重跑命令。缺失私有资源必须标记 SKIP，不能换资产仍宣称同一 capture 通过。

当前尚无新 GPU Scene 候选，因此没有新基线审批记录。正式候选验收前须完成上表的输入
冻结和逐项容差审批；阶段 2/3 的差分按已批准版本执行，修改合同需明确记录原因与重新审批。
