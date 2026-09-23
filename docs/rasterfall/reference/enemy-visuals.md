# 敌人视觉资产与姿态合同

> 状态：当前
> 所有者：感染体资源选择、刚性轮廓与展示数据

玩法和 renderer 的所有权见[角色表现架构](../character-presentation.md)，生成与截图见[指南](../guides/enemy-visuals.md)，V1/V2 实施与验收现场见[历史记录](../archive/enemy-visuals-v1.md)。

## 普通感染体

COMMON、FAST、HEAVY 对应原有 gameplay enemy type。renderer 本地选择 `AUTO`、`LEGACY`、`BLOCK_INFECTED` 或 `HUMANOID_INFECTED`；AUTO 的 Block/Humanoid 比例依次为 Common 60/40、Fast 30/70、Heavy 40/60。slot 是稳定展示种子，选择结果不进入 gameplay 或网络。资源失败时只报告一次并回退旧身体。

六份 `rf_infected_<family>_<type>.rmesh` 使用 RFCHAR V1 的 21 stable roles、29 bones（含八附件）和 RFM2 v14；完整格式见[角色资产合同](character-assets.md)。Block 使用方块身体，Humanoid 复用 V2 身体。Fast/Heavy 的比例变形同时作用于顶点、rest joints 与 socket anchors，保持 inverse bind 一致。每个 Block 最多 600 三角形、Humanoid 最多 1800 三角形，材质最多 9 个，不使用纹理采样。

源码是 `tools/blender/generate_rasterfall_infected.py`；manifest 位于 `tools/assets/manifests/enemies/`，公开资源位于 `rasterfall/assets/models/enemies/`。GLB 位于本地 `private-assets/source/enemies/`，运行时不解析 manifest，也不依赖 Blender 或私有源。模型的衣物、感染伤口和生长物都是程序化几何与纯色材质，无外部模型、图片或纹理。

普通感染体通过 RFCHAR、procedural bone pose 与 CPU skinning 展示，不能当作 rigid-part animation。每项 recipe 缓存 immutable resource 和串行 scratch instance；提交前重置 pose。轻量 per-slot cache 只保存位置、步幅和短移动保持；真实位移驱动步幅，重复 render 不推进动画。资产自带 Heavy 体型，renderer 不重复套旧 1350 缩放。普通感染体远裁剪为 56000 RFU；它不是索敌或射程。旧模型保留给显式展示入口。

## 特感刚性 profile

Smoker、Charger、Tank 使用 `include/rasterfall_enemy_rig.h` 的固定 channel 与 `src/render/rasterfall_enemy_rig.inc` 的 profile、truth adapter、pure sampler 和 generic renderer。默认新增特感沿用 procedural rigid rig；只有明确需要 mesh deformation、skinning 或刚性组件无法表达的复杂关节动作时才评估 skeletal runtime。

局部坐标为 RFU、+Y 上、+Z 前、角度制旋转，根底部 -900。`enemy_rig_profile` 拥有尺寸、parent-relative bind pivots、body parts 与步幅；`enemy_rig_part` 是一个有 channel、中心、半尺寸和颜色的刚性切角棱柱；pose 提供 ROOT/TORSO/HEAD、双侧 upper arm/forearm/hand 和 leg。子节点先旋转，再叠加 bind pivot/translation、父节点、profile 尺寸、敌人 yaw、world translation 与地面/空中 lift。旧 scale 仅用于 legacy death squash。

Smoker 是高瘦、长臂、前探头和不对称左肩；舌头从 finalized HEAD pose 取得起点。Charger 是巨大左臂和低伏冲撞轮廓，右臂萎缩；Tank 是低重心宽胸、短粗腿和双拳，横扫由近端旋转带动远端。

Charger 的 windup 取 `charge_active && special_timer_ms > 0`，命中仅取新增 `charge_hit_actor_mask` 位；observer 对边沿去重并在 180 ms 内采样 impact，冷却后 420 ms 恢复。Tank 从 `charge_elapsed_ms` 与 `TOY_CONFIG_TANK_IMPACT_MS`、`TOY_CONFIG_TANK_WINDUP_MS` 采样蓄势、冲击、后摆与恢复；挥空也走完整动作。命中粒子与击飞轨迹只消费权威目标与结果。网络协议 43 显式编码权威命中 mask；profile、pose、observer 和粒子不入网。Charger 620 RFU 的玩法碰撞代理由玩法拥有，不从渲染几何推导。

扩展 Hunter/Boomer 时先定义可在远景与单色图辨认的轮廓，再增加 profile、locomotion 与 sampler；adapter 只读真实动作和计时字段。动态组件从 posed channel 取起点，命中效果消费真实结果。变更后按[验收指南](../guides/enemy-visuals.md)增加 fixture 并检查确定性与 gameplay 不变性。
