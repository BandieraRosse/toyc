# Enemy Visual / Procedural Animation Framework V1

> 文档更新：2026-09-12
> 源码核对基线：工作区（Smoker / Charger / Tank rigid profiles、truth adapter、procedural pose 与 generic prism renderer；固定关键帧/轮廓/world capture；协议 43 显式命中 mask；保留六份普通感染体 RFM2）

## Enemy visual pipeline 与所有权

```text
toy_game enemy gameplay truth
        ↓
presentation adapter (enemy_rig_adapt / enemy_rig_observe)
        ↓
procedural pose (enemy_rig_sample)
        ↓
visual profile / rigid body parts
        ↓
generic renderer (render_enemy_rig)
        ↓
special components / VFX (posed mouth, actual hit mask → effects)
```

默认新增敌人技术路线是 **procedural rigid rig**，不是 RFCHAR。模型采用 low-poly rigid
components，silhouette first：几何比例直接传达玩法身份。姿态改变刚性组件整体变换，
没有 mesh deformation、skinning、bone weights、RFANIM、IK 或通用动画图。
只有确实需要网格变形、蒙皮，或 rigid parts 无法表达复杂关节动作且收益明确时，才评估 skeletal runtime。

代码从 `include/rasterfall_enemy_rig.h` 的固定语义与结构开始，再读
`src/render/rasterfall_enemy_rig.inc`：上部 catalog 是模型，中部 adapter / sampler 是动画，
末尾是通用绘制。该 include 复用现有 renderer primitive/death 链路，不增加编译单元。
`rasterfall_render.c::render_enemies()` 仍拥有遍历、裁剪、受击位移、地面/空中锚点和死亡表现。
地图展示台的三个旧命名入口只薄封装同一个 rig，不再定义几何或攻击动作。

| 结构 | 职责与单位 |
| --- | --- |
| `enemy_rig_profile` | type、parent-relative bind pivots、body parts、步幅参数、模型自身尺寸 |
| `enemy_rig_part` | 一个刚性切角棱柱，channel、中心、半尺寸、颜色；不持有 timer |
| `enemy_rig_pose` | ROOT/TORSO/HEAD、左右 upper arm/forearm/hand、左右 leg 的 translation/pitch/yaw/roll |
| `enemy_rig_input` | 从真实能力字段适配的瞬时输入；sampler 无副作用 |
| `enemy_rig_observer` | 每槽位只记位移步幅、短移动保持与最新观察到的命中时刻；不是攻击状态机 |

局部坐标是 RFU、+Y 向上、+Z 前向，旋转单位是度；根底部为 -900。
子节点先旋转，再加自身 bind pivot / translation，然后向父节点组合；最后应用 profile 尺寸、
敌人 yaw、world translation 与 ground/airborne lift。手随前臂、前臂随上臂、上臂随 torso。
未使用的 channel 保持零。living 尺寸由 profile 拥有，旧 `scale` 只用于 legacy death squash，
避免 Tank/Charger 再被外层旧缩放重复放大。现有 hurt tint、ballistic death、fade 保留。

## 三个正式 rigid 用户

- Smoker：高瘦窄胸、长腿长垂臂、前探头与驼背，左肩感染块造成不对称；较长步幅与 torso sway。
  舌头仍为动态 presentation component，起点从同一个 finalized HEAD pose 求得；本轮没有独立 smoke emitter。
- Charger：左肩/上臂/前臂/拳头组成巨大冲撞侧，右臂萎缩，torso 侧倾，普通比例腿；不用装甲区分 Heavy。
  巨臂走路摆幅小，冲锋时压低躯干、抬起巨臂、抑制腿摆，形成稳定冲撞面。
- Tank：极宽短胸、粗腰、短粗腿、双粗臂与大拳、小头无明显颈；低重心、最长步幅、低幅摆臂。
  固定右手 sweep；本轮未实现左右交替，捕获不使用随机动作选择。

## 攻击真值与表现

Gameplay owns authoritative timing；presentation samples gameplay state。
不在 renderer 决定是否攻击、是否命中或谁受到伤害。

Charger `charge_active && special_timer_ms > 0` 是 windup；其 timer 归一化驱动下沉、
前倾、巨臂收紧。timer 结束后采样稳定低伏 charge。`charge_hit_actor_mask` 新增位才是
成功撞击：observer 记录当时的 `charge_elapsed_ms`，180ms 内叠加 torso 扭转和拳头前顶。
重复 render 不重触发；命中不终止冲锋。真实冷却开始后的 420ms 衰减冲锋姿势，作为 recover。
撞墙但没有新命中位不会伪造 actor hit；不调整 authoritative collision、damage 或 knockback。

Tank 的 `charge_elapsed_ms` 直接采样五个姿态关键点：开始、蓄势结束（impact 时间的 2/3）、
`TOY_CONFIG_TANK_IMPACT_MS`、`TOY_CONFIG_TANK_WINDUP_MS`、结束后 420ms。
当前常量对应 625ms 伤害峰值、750ms 进入冷却。torso 先扭转蓄势，肩臂随后加速，拳头沿
跨过正前方的弧线运动；625ms 对齐冲击姿态，继续转到 750ms，再由冷却剩余时间推导 recover。
头部反向补偿 torso yaw，另一只手支撑平衡；近端旋转带动远端，不独立平移拳头来假装横扫。
sampler 不使用命中 mask 决定是否挥空，挥空仍走完整动作。

`rasterfall_effects_sync_enemy_feedback()` 对 Charger/Tank 的新增命中 mask 位，在实际目标位置
调用现有 hit particle runtime，重复同步去重。目标击飞仍是 gameplay 原有行为；本地受伤
camera shake / 屏幕反馈继续消费现有 HP edge。没有新增音频资源或攻击音频 hook。
网络协议 43 仅给 enemy snapshot 增加既有权威 mask 的 8 字节显式编码，客户端 apply 原样复制；
pose、model profile、observer 与粒子不入网。快照到达才可观察到远端命中，不承诺本地亚帧同步。

## 固定验收与扩展流程

沿用 `build/rasterfall --enemy-visual-capture <dir>`，原家族截图仍保留。
新增 `rig-<type>-<state>-<view>.bmp`，六种敌人 idle 使用同一相机、地面、尺寸单位与朝向；
三特感另外捕获 Smoker idle/walk，Charger idle/windup/charge/impact/recover，
Tank idle/windup/pre-impact/impact/follow-through/recover。
每个状态有 front / three-quarter / side / silhouette 与 world-near/mid/far。
silhouette 是 rigid submission 的单色覆盖，不改几何；普通 family 对照仍保持原材质。
isolated 距离 3600 RFU；world 在正式地图南侧开阔区域固定同一目标和 FPS 眼高，
距离 3500/7000/14000 RFU，无随距离放大，背景、遮挡、深度由正式 scene 绘制。

fixture 用正式 typed spawn 创建敌人，然后冻结明确的能力字段与位移相位；不是 gameplay 回放。
每帧检查 gameplay byte copy 不变、rig submission 数与 command overflow。
命中边沿、重复 render 与冷却 adapter 门禁随 capture 执行；网络 mask codec 由 logic test 验证。
`tools/enemy_visual_round.py --capture --deterministic` 重拍并比较所有 BMP，复用已有组图工具
输出 `rig-lineup.png`、`rig-silhouettes.png`、`rig-{smoker,charger,tank}-{poses,world}.png`。
保留原始 BMP；原尺寸远景才是可读性判断依据，组图只是审阅工具。

新增 Hunter / Boomer 的默认顺序：

1. 明确 silhouette 与 gameplay identity；先对照远景/单色图调整比例。
2. 在 `enemy_rig_profiles` 注册 profile，定义 bind pivots 与 body part catalog。
3. 设置 locomotion 参数，必要时在 pure sampler 增加该类型的姿态采样函数。
4. adapter 只读真实 gameplay action/timer，映射 crouch/launch/airborne/landing 等已存在语义。
5. 特殊动态组件从 posed channel 获取起点；真实命中/死亡结果接现有 effects runtime。
6. 在现有 capture fixture 增加固定状态，更新组图覆盖；检查零 gameplay mutation 和确定性。
7. 查看 isolated、world near/mid/far 与 silhouette，调整后再次 capture。
8. 同步本页与 navigation；新增 include 时检查 Linux/self 依赖、Windows 自动头依赖。

无需复制专用 renderer。只有出现本轮固定 channel/primitive 无法表达的真实需要时才扩展 rig；
不要为了假设中的 Hunter/Boomer 提前建立动画引擎。

## 保留的普通感染体家族（Enemy Visual V2）

本轮迁移仅 Smoker / Charger / Tank。Common/Fast/Heavy 的 legacy block/round
body-part table 继续使用原 primitive 路径；BLOCK_INFECTED / HUMANOID_INFECTED 继续使用
已存在的 RFCHAR + procedural bone pose + CPU skinning。后者并非 procedural rigid-part animation，
不得因为外观低模就把它记录成 rigid rig。它们不是新增特感的默认实现模板。

## 状态与接入入口

`toy_game_enemy.type` 仍由玩法拥有。COMMON、FAST、HEAVY 对应现有
`TOY_GAME_ENEMY_PURSUIT_*`；AI、spawn、伤害、波次、snapshot 与地图均不增加字段或分支。
`rasterfall_enemy_visual.h` 定义本地展示选择 AUTO / LEGACY / BLOCK_INFECTED / HUMANOID_INFECTED。
默认 AUTO 由 renderer 按 enemy type 混合家族，进程启动参数只改变本机 renderer；联机双方可以选择不同外观。

AUTO 的比例为 COMMON 70/20/10、FAST 40/40/20、HEAVY 30/40/30，顺序均为
LEGACY / BLOCK_INFECTED / HUMANOID_INFECTED。slot 只作为 renderer 稳定选择的种子，避免帧间
闪烁；结果不写入 gameplay enemy 或 network snapshot。特感独立走上面的 procedural rigid rig。

`rasterfall_render.c::render_enemies()` 继续拥有可见性、地面/空中锚点、受击位移和死亡表现编排。
它在旧身体分支前调用 `render/rasterfall_enemy_visual.inc::render_infected_enemy()`。
六项 `enemy_visual_recipes` 是 family + enemy type → 公开 model path 注册表；特感使用独立 rigid profile。
普通家族资源加载失败只报告一次并回退旧身体，不回写 gameplay。

每项缓存一份 immutable resource 和一份串行 scratch instance。每次提交先 reset pose，再根据
stable humanoid roles 写入基础感染站姿/步姿，finalize bones 后使用现有 gallery vertex cache、
CPU skinning、材质和 Lighting V1。scratch 不跨敌人保留动作；逐槽位的轻量 presentation cache
仅保留上次位置、步幅相位和短暂移动保持时间。真实位移驱动步幅，重复渲染不会加快动画；
短暂不更新位置的帧保留移动姿态，停止后恢复 idle。没有新 clip 格式、ragdoll 或动作状态机。

新 RMESH 的纯色三角形经过既有 `draw_world_triangle()`，沿用 Enemy Death Presentation 的
旋转、位移与 alpha；旧死亡样式保留纵向压缩。资产自身已拥有 Heavy 体型，不重复套用旧
Heavy 的 1350 缩放。受击 tint 仅在当前 submission 生效。模型与附件在资产层变形，renderer
只按 header 的 position_scale 换算到 512 RFU/m，不用显示偏移修补 bind。

感染家族普通敌人的远裁剪为 56000 RFU，以便观察真实 100m 投影；旧家族、特感与队友保留
原有裁剪。远距离不放大模型。现实地图墙体仍会遮挡敌人，此范围不是射程或索敌距离。

## 资产来源、设计与预算

`tools/blender/generate_rasterfall_infected.py` 是可复现源。它复用 V2 的 armature、geometry
helpers、skin export 和 skeleton patch；Humanoid 身体复用 V2 `create_body()` 并减少环面侧数，
Block 使用独立方块身体。无外部模型、图片或纹理；感染伤口、肋骨、残甲与生长物均为源几何，
材质为 GLB 内的纯色色块，导入后进入 RFM2 material table。

| 类型 | Block-derived Infected V1 | Humanoid-derived Infected V1 |
| --- | --- | --- |
| Common | 方块人、破损下颌、裸露小腿、单侧肩部感染、破损衣摆 | V2 解剖轮廓、衣物色块、单侧肋部伤口、面部感染 |
| Fast | 窄躯干、长臂、钩状手、背鳍、前倾追击姿态 | 瘦化 V2、长肢与暴露生长物、同一追击姿态 |
| Heavy | 宽厚躯干、残甲、巨大单侧前臂与背部突起 | 宽厚 V2、肩甲/胸甲/背甲/膝甲、非对称结构变异 |

生成与验证工具约束每个 Block 不超过 600 三角形、Humanoid 不超过 1800 三角形，材质不超过
9 个，无纹理采样。实际数量、文件字节数及门禁结果写入生成目录 `asset-report.json` 和日志，
不把阶段性测量值作为稳定文档状态。

六份 `tools/assets/manifests/enemies/rf_infected_<family>_<type>.asset.json` 沿用 character schema；
GLB 中间源在本地 `private-assets/source/enemies/`，公开交付物为
`rasterfall/assets/models/enemies/rf_infected_<family>_<type>.rmesh`。
全部保持 RFCHAR V1 的 21 stable roles、29 bones（含八附件）和 RFM2 v14；腿、手、头部、附件
同源导出。Fast/Heavy 的比例变形同时作用于源顶点、rest joints 与 socket anchors，保留 inverse bind
一致性。Block 的衣物腰部保留 spine/chest 双权重，四肢与感染附件大多使用单骨权重。

运行时不解析 manifest，不要求本机存在 Blender、GLB 或 private-assets。根 Makefile 的递归公开
资产扫描自动覆盖六份 RFM2；Windows package 递归复制公开 assets。新增的是 renderer include，
不是独立编译单元；Linux/self 的显式依赖和 Windows 的自动头依赖均覆盖该入口。

## 使用与验收

```sh
make app-rasterfall app-glb-inspect build/rfchar_runtime_test
build/rasterfall --enemy-visual-family block-infected
build/rasterfall --enemy-visual-family humanoid-infected
build/rasterfall --enemy-visual-family legacy
build/rasterfall --enemy-visual-capture tmp/enemy-visual-mix-v1/mixed
build/rasterfall --enemy-visual-family legacy --enemy-visual-capture tmp/enemy-visual-mix-v1/legacy

# 重新生成六个源模型、统一导入、验证及截图
python3 tools/enemy_visual_round.py --generate --capture --deterministic
# 已有公开 RFM2 时仅验证与截图，不要求 Blender
python3 tools/enemy_visual_round.py --capture --deterministic
build/rasterfall --logic-test
```

独立 capture 入口为 `--enemy-visual-capture <output-dir>`，默认捕获 AUTO mixed；也可与三个
family 参数组合进行强制家族验收。捕获标签会显示 `COMMON / BLOCK_INFECTED` 这类实际选择。
实现位于 `dev-tests/rasterfall_enemy_visual_capture.inc`：加载正式地图，用
`toy_game_spawn_horde_type()` 生成三种真实 enemy slot，调用 `toy_game_update_world()` 验证移动，
随后冻结 fixture 以便比较 bind/idle/move × front/side/three-quarter。
截图始终通过正式 `render_enemies()` 分支，逐帧比较完整 gameplay 结构的字节副本，并检查模型
submission 数、资源失败和 command overflow；缺少资产时验收失败，不允许回退图冒充新资产。

默认输出 `tmp/enemy-visual-v2/`：六份 `<family>-<type>.png`、`six-infected.png`，以及每家族的
world/death 组图、原始 BMP、asset report、contract/import/runtime 日志。`--deterministic` 重拍并
逐字节比较。已有源 GLB 时执行 manifest 和 GLB contract 门禁；只有公开 RFM2 时仍执行 runtime
隔离、蒙皮和骨架/附件门禁以及三角形/材质预算。

实景 10m、30m 使用正式地图南侧空地与 900 RFU 眼高；100m 因真实地图外围墙遮挡，单独输出
`distance-100m.bmp`，只保留天空/地面并明确标记 UNOBSTRUCTED DISTANCE。模型、相机焦距和
分辨率不随距离缩放。100m 的类型可读性应以原尺寸投影审阅，不能把放大截图当作 FPS 实际视图。
死亡组图观察既有 ballistic/fade 与 legacy collapse，不新增死亡动画。
