# Static World Lighting V2 — Phase C2

> 文档更新：2026-09-14
> 源码核对基线：Phase C1 工作区基础上的动态实体接入；核对 `rf_game_render()`、actor/enemy normal submission、独立 viewmodel 与材质命令回归。

## 正常消费链与所有权

`rf_game_render()` 在 normal world submission 前调用 `rasterfall_render_begin_dynamic_lighting()`，
在 interactable submission 后、VFX 前结束 scope。scene light 是串行 renderer 的帧内派生值，不进入
actor、RFCHAR instance、session 权威状态或协议。本地玩家按 gameplay x/z、ground_y 采样一次，
第三人称 managed player 与 viewmodel 共用这个 Q8 值。airborne、受击视觉偏移和骨骼不改变采样位置。

| 正常 consumer | 接入入口 | 后续路径 |
| --- | --- | --- |
| AI teammates，包括正式 RF 小队、Hurd、Maid | `render_ai_teammate()` 每可见 actor 一次 root sample | modular RFCHAR body/rigid gear、旧骨骼模型、procedural 回退 |
| 远端玩家，host/client | `render_network_teammate()` authoritative actor root sample | 原 interpolated avatar geometry；不从相机位置决定环境 |
| 本地第三人称 | `rasterfall_render_managed_player()` 共用本地 sample | 原 procedural avatar |
| COMMON/FAST/HEAVY 与 pursuit 类型 | `render_enemies()` 主体前一次 gameplay enemy root sample | infected RFCHAR/form/material、legacy procedural 资源失败回退 |
| Smoker/Charger/Tank | 同一 enemy scope | procedural rigid rig |
| 世界持有武器 | owner scene override | 原 actor model weapon、skeletal/modular weapon 与 procedural weapon |
| 无 owner 的地图武器拾取物 / 投出的 bomb、molotov 主体 | `render_interactables()` / `render_projectiles()` 每实体位置采样 | 现有模型/material；不包括 explosion/fire VFX |
| 第一人称武器、手臂与 procedural pill | `rasterfall_viewmodel_render(..., scene_light_q8)` | 原屏幕空间 flat/textured submission；scene clamp 192..256 保留可读性 |

场景 factor 通过现有 `active_model_scene_light_override_q8` 进入 RMESH 与 primitive helpers，
每 owner 提交结束恢复。武器不查询 field，没有 weapon cache/API。现有 form lighting 不变；
角色 material policy 在 scene × form 后夹取 FACE/SKIN/EYES/HAIR 下限（原 224/224/240/176 不变），CLOTHING/EQUIPMENT
保留 form 与完整环境响应。死亡透明主体也消费同一 sample；死亡 presentation 偏移不重新采样。

Smoker tongue、blob shadow 在 entity scope 之前；已有 muzzle flash 几何临时恢复原 lighting。
粒子、火焰、tracer、爆炸与 overlay 不进入 scope。没有修改 field/bake/参数、玩法、AI、动画、协议、
模型格式、资产或几何。

## 诊断例外与复核

独立 gallery、Character Acceptance、enemy visual acceptance、procedural humanoid、model captures
不启用 normal dynamic scope，继续固定/专用 lighting。Campaign developer strip 和 humanoid debug
属于 renderer-only diagnostic fixture，同样保留专用策略。`--character-world-capture` 仍是隔离角色
观察，环境接入由 `--environment-capture` 验证。

`--environment-capture` 保留原设施视角，Campaign 另输出 `c2-open`、`c2-shadow`、`c2-reopen`：
另有六类敌人的单体 shadow capture；固定 modular/procedural actors、六类敌人和本地 viewmodel 经过正常 consumer；shadow anchor 由当前
field 与 gameplay collision 只读选择。重复 warm fixed-input frames 比较 baseline/V2 frame、raster、
actor/enemy submission、commands 与查询计数。计时不包含 BMP IO、present 或 pacing。

`--logic-test` 复用原模型命令回归，检查 flat/textured 的 scene × form 后 material floor：既防止
环境覆盖下限，也防止可读性材质完全绕过环境。可复核日志与图放在
`tmp/static-world-lighting-v2-phase-c2/`，不把阶段性测试数量写入稳定导航。

Phase C2 到此停止；不进入 C3，不调整 Phase D 参数，不自动提交。
