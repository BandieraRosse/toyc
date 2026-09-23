# Temporary Campus Kit V0 资产规格

> 状态：当前
> 所有者：临时校园展示资产与拼接合同

本套件用于可撤回的 Planar Massing V0 展示，不代表武汉大学的实测宽度、坡度、标高或正式建筑。阶段清点与合成场景观察见[历史记录](../archive/temporary-campus-kit-v0.md)；重建与截图见[操作指南](../guides/temporary-campus-kit-v0.md)。真实空间依据仍由 [Return to WHU 资料](return-to-whu-core/)负责。

## 资产分工

复用 `rf_railing`、`rf_lamp_post` 与现有无纹理 surface 表达边界、灯、道路、广场、绿地和屋面。`rf_arch_wall`、`rf_arch_beam` 只适合作次要边界或隐藏跨度；工业 overlay 不作为校园原生设施。校园主立面使用下列独立资产。尺寸顺序为 Blender X/Y/Z 宽、深、高，单位 m；均为 authored 模数。

| 资产 | 尺寸 m | 用途 |
| --- | --- | --- |
| `rf_campus_wall_plain` | 4 × 0.24 × 3.2 | 盲墙、侧墙、转角与叠层外壳 |
| `rf_campus_wall_window` | 4 × 0.24 × 3.2 | 整层墙窗；窗底 1 m、窗高 1.4 m、1 m 窗格节奏 |
| `rf_campus_window_strip` | 4 × 0.24 × 1.4 | 自定义层高外壳的独立窗带 |
| `rf_campus_entrance` | 4 × 1.2 × 3.2 | 2 m 宽、2.5 m 高开口和简洁雨篷 |
| `rf_campus_roof_edge` | 4 × 0.6 × 0.3 | 连续檐口；屋面仍用 surface |
| `rf_campus_column` | 0.4 × 0.4 × 3.2 | 同楼层高度普通柱 |
| `rf_campus_stair_short` | 4 × 1.2 × 0.6 | 四级、0.30 m 踏步、0.15 m 踢面 |
| `rf_campus_stair_long` | 4 × 2.4 × 1.2 | 八级、同踏步尺寸 |
| `rf_campus_retaining_wall` | 4 × 0.4 × 1.2 | 平台边缘与挡土墙 |
| `rf_campus_curb` | 4 × 0.2 × 0.15 | 道路与人行道高度边缘 |
| `rf_campus_sidewalk` | 4 × 2 × 0.15 | 可按 4 m 拼接的人行道 |
| `rf_campus_tree_proxy` | 3 × 3 × 5 | 绿化尺度与远景识别 |

## 拼接、坐标与碰撞

统一底面中心 pivot、identity transforms、米制源尺寸和 4 m 长边模数；placement 使用 yaw 与 scale=1000。Blender Z-up/-Y-forward → GLB Y-up/+Z-forward → RMESH 232 units/m → registry 2207 milli-scale → world 512 RFU/m。量化约 4.3 mm，拼接使用完整性报告的 `snap_dimensions_rfu`：3.2 m 墙高为 1637 RFU，4 m 长边间距为 2048 RFU。窗带按 1 m 节奏拼接，不用浮点层高再次取整。

楼梯 GLB +Z 端低、-Z 端高；yaw=180 时从 -Z 向 +Z 上升。pivot 位于投影底面中心，短、长低端距 pivot 分别为 0.6、1.2 m。台阶仅表达玩家尺度，不是校园实测阶数。

资源是双面可见的开放视觉壳：box 省去背面与底面，墙窗省去隐藏顶面，窗带省去内部相接侧面；台阶仅保留外露面。它们不是水密模型、室内或物理体积。树冠为固定三角化粗面代理，三角形规范排序。色块采用灰白墙、浅灰混凝土、低饱和蓝灰不透明窗、灰道路、绿地和土红跑道，无新纹理。

展示 ID 为 24–35，registry `collision_size` 全零；`collision_dimensions` 拒绝为这些资源生成 AABB。入口与楼梯不可将展示包围盒当作通路碰撞，玩法承载另设显式 collision 记录。

## 来源与所有者

几何由 `tools/blender/generate_campus_kit.py` 生成，manifest 位于 `tools/assets/manifests/props/campus/`，公开 RMESH 位于 `rasterfall/assets/models/props/campus/`，注册表位于 `rasterfall/include/rasterfall_prop.h` 和 `rasterfall/src/rasterfall_prop.c`。资源为仓库自制程序化几何，无外部图片、校园照片或纹理输入。源 GLB/Blend 是本地产物，不是运行依赖。
