# 玩法、会话、地图与 AI

> 文档更新：2026-09-10
> 源码核对基线：工作区（敌人 dying slot 生命周期 1000ms；Jesus 使用稳定 RF Rifleman identity；两个正式四人 squad roster 已分别编入中央/东部旗帜；model resource/instance/gear/palette 仍只属于 presentation；其余玩法真值不变）

> 源码核对补充：正式 Hurd 四人使用专用 character IDs；原 Maid 四人旗卫在 flag 1 原位恢复并使用 Maid character/profession；普通 player、Eula、佣兵为 NONE；固定角色索引、HURD 旗帜 assignment 与派生 control status。

地图初始普通队友 Jesus 现在携带 `RASTERFALL_CHARACTER_RF_RIFLEMAN` 稳定视觉身份。`toy_game_actor`
仍只保存 character ID、位置/朝向、武器与 animation semantic/time 等玩法真值；共享 body/gear resource、
per-actor model instance 和 palette 均由 renderer 的轻量 character presentation runtime 管理。资源缺失、
recipe 缺失、instance 初始化失败或当前 downed path 不适用时，渲染器回退既有 procedural actor，
不改变 AI、伤害、网络或 session simulation。

## Formal RF squad roster

`rasterfall/include/rasterfall_roster.h` 与 `src/rasterfall_roster.c` 保存两套固定有序游戏内容：
Standard Response Squad 为 Jesus、Squad A Medic、Squad A Engineer、Squad A Recon；Assault Squad
为 Squad B Rifleman、Squad B Breacher、Squad B Heavy、Squad B Medic。character ID 在
`rasterfall_character.h` 中独立且稳定，职业只通过 character profile 的 presentation recipe 映射，
不作为 actor 类型或 gameplay ability。

`rasterfall_session_reset()` 保留地图已有 Jesus actor 并把它登记为 Standard Response 的第一位，
再通过普通 `toy_game_add_ai()` 创建其余七名 actor；session 只保存 squad 到 actor index 的轻量
运行时定位。正式 V2 roster 的战斗武器固定为 AK，以便与出生点 V2 动作展示使用同一武器资产和双手挂点；旧模型/程序化角色仍按 AI 等级选择武器。AI、武器、动画、伤害、碰撞和网络规则继续使用原有 actor 路径，actor 不携带 model、gear、
RMESH 或 attachment 数据。

两套正式小队在 reset 时各自绑定一面固定旗帜：Standard Response 使用 `RESP` 旗帜（`(0,7000)`，
战场中部北侧空地），Assault 使用 `ASLT` 旗帜（`(14000,0)`，东部空地）。四名成员分别使用旗帜的
四角部署槽；这两面旗帜不占用原中央基地、Maid 或 Hurd 的旗帜索引。坐标依据正式地图布局导出核对，
并避开中央基地、东侧刷怪带和东侧走廊。

## 三层职责

`lib/game.c` + `include/toy_game.h` 是确定性玩法核心：武器/敌人定义、移动碰撞、地面与坡道、
射击和投射物、波次、事件、角色和敌人 AI、导航等。这里不依赖窗口或渲染器，适合最小逻辑回归。

`src/rasterfall_session.c` + `include/rasterfall_session.h` 是模式编排层：加载/重置关卡，构建和执行
玩家命令，商店与雇佣 AI，剧情阶段、托管角色，以及主机/客户端不同的 step/replay 路径。

`src/rasterfall_ai.c` + `include/rasterfall_ai.h` 管理可插拔 AI 注册表，把 observation 交给控制器并
将 decision 同步回游戏；具体内建战斗和移动规则大量仍在 `lib/game.c` 与 session 的托管 AI 中。

本地玩家现在是 `toy_game.actors[TOY_GAME_PLAYER_ACTOR_INDEX]`（当前为槽位 0）的正式
`TOY_GAME_ACTOR_PLAYER`；远端人类也占用固定 actor 槽位。位置、生命、空中状态、库存/武器计时、
统计、动画、倒地/复活和特殊控制均持久化在 actor 中。
正式世界规则、session、HUD 和展示层都直接读取 actor；不存在独立的玩家状态副本或仅供玩家使用的
规则入口。session 的本地 step 与 client prediction 已直接操作 actor 的
移动、跳跃、airborne、武器/reload/fire cooldown、投掷物、animation 和 special-control；camera.body
始终由 actor 派生。AI 分配从槽位 1 开始，网络协议和远端 actor 槽位不变。

session 的本地复活、商店控制锁、交互死亡判断和托管武器决策读取 actor 状态；出生点、付费复活
和正式 world step 也直接写入或推进 local actor。

## Hurd Relay gameplay foundation

session reset 先在原坐标 `(-12000, 0)` 和原 flag 1 恢复四名 `ANIME_GUARD_*` Maid 旗卫；四人保留
原 `anime_character_id=2..5`、slot offset、部署和旗卫配置，并统一通过 actor 的
`character_id=RASTERFALL_CHARACTER_MAID` 解析为 Maid profession。Hurd 改用 flag 2，避免占用 Maid 的
稳定旧配置。

session reset 在正式 world 中创建固定 Gunsmith、Logistics、Medic、Guard 四名普通 AI actor，并明确
写入四个 Hurd 专用稳定 `character_id`；前三人初始使用 Pistol，Guard 使用 SMG。普通 player、地图佣兵、
商店 hired AI 以及 Eula actor 使用 `RASTERFALL_CHARACTER_NONE`，不按 actor slot 或 class 随机
选择 Hurd identity。四人设置 `flag_guard`，因此不进入
旧商店重新指派列表，也不受“清除雇佣 AI”影响；其移动、部署、防守、战斗、受伤、DOWNED、REVIVE、
动画和武器仍全部走现有 actor 规则。`hurd_outpost.squad_actor_indices[]` 只供 session 后续剧情定位固定
角色，不承担 assignment；唯一 assignment truth 仍是 `actor.flag_index == HURD flag index`。

北侧 HURD 旗帜初始部署于 `(0, 28500)`，固定 control region 为
`x=-5000..5000, z=25500..31500` RFU。`rasterfall_session_hurd_status()` 是无副作用派生查询：旗帜必须
active、未被携带且位于区域内；assigned count 沿用普通旗帜语义统计 `actor.flag_index`，capable count 只统计
`ALIVE && hp > 0`。`controlled = flag_deployed_in_region && capable_count > 0`，不保存第二份 controlled
状态。DOWNED 仍计入 assigned 但不计 capable；复活任一 assigned guard 会使下一次查询立即恢复控制。
该查询是下一轮 Tactical Map 和 Hurd pressure 的只读接口，二者不应自行维护控制状态。

击飞由 actor/enemy 的 `vertical_velocity`、`airborne_y` 与恒定水平初速度共同推进：垂直方向逐步施加
重力，水平方向在落地前保持速度，因此世界空间轨迹为抛物线；最终距离由水平初速度和实际滞空时间
自然决定。player/actor 的单帧空中强制位移按不超过碰撞半径一半的子步扫掠，子步使用累计目标
坐标并在本逻辑步的新旧高度间插值；X/Z 仍分轴处理，受阻轴会清除对应 knockback 速度。玩家的击飞冷却不依赖旧的
`toy_game_update_held()`，本地主机 world step 与客户端 actor motion 都会推进它。击飞扫掠一旦某个轴
受阻就锁定该轴，不能在后续子步越过障碍；敌人击飞按自身碰撞半径分段扫掠，在逻辑步的新旧高度间
插值，撞墙时仅清除受阻轴的水平速度。Charger 的每轮 `charge_hit_actor_mask` 只负责同一 Charger
去重；来自不同 Charger 的撞击不受玩家击飞冷却阻挡。Charger 和 Tank 的击飞水平目标距离分别为 9m 和 6m。

## 地图链路

- `include/toy_map.h`：磁盘地图解析后的通用结构。
- `lib/map.c`：文本 `.map` 解析器；新增语法或字段从这里开始。
- `include/rasterfall_map.h` / `src/rasterfall_map.c`：把地图绑定为玩法盒体、图元、prop 碰撞、可交互物和安全区。
- `assets/maps/rasterfall.map`：正式公开关卡数据。
- `rasterfall_render.c`：只负责把地图结构画出来；不可用视觉几何代替玩法碰撞。

## 按任务查找

- 武器数值、弹药、射速、价格：`toy_game.h` 的 weapon 枚举/结构和 `game.c` 的武器表与操作。
- 敌人类型、技能、波次：enemy 枚举/信息表、wave plan、enemy update 路径。
- 移动、坡道、跳跃、碰撞：`toy_game_query_ground`、`position_blocked`、motion/navigation 相关函数；静态 prop 在 `lib/map.c` 中按 RFU profile 生成普通 box，导航消费同一 primitive。
- 玩家输入产生何种动作：`rasterfall.c` 构造 command，session 执行，game 落实规则。
- 商店、剧情、队友雇佣、托管玩法：`rasterfall_session.c` 的 `shop`、`campaign`、`managed_ai` 区域。
- 角色外观选择：`rasterfall_character.c`；角色动作状态仍由 `toy_game_actor.animation` 等字段拥有。
- HUD 显示错误：先确认 `rasterfall_hud_state` 在主循环中是否正确填充，再改 `rasterfall_hud.c`。

当前敌人目录不再包含独立的 `COMMON` 和 `HEAVY` 基础类型；普通刷怪统一使用
`PURSUIT_COMMON`，重型刷怪统一使用 `PURSUIT_HEAVY`。`wave_waiting_common` 与
`wave_waiting_heavy` 仍是追击型波次的统计字段，不代表已删除的敌人类型。

敌人 AI 现在分为两条路径。`PURSUIT_COMMON`、`PURSUIT_HEAVY` 和 `PURSUIT_FAST` 只做最近有效目标选择、导航追击和攻击距离判定，不再经过视野方向、观察、警戒传播、枪声调查、丢失目标或搜索状态。`SMOKER`、`CHARGER`、`TANK` 仍进入各自的特感更新函数，并保留技能的独立索敌、前摇、冲锋/束缚/横扫逻辑。Smoker 对主机玩家和 AI 队友统一使用 `4000ms` 拉拽；拉拽结束、目标失效、失去视线或玩家近战打断后进入 `8000ms` 冷却，冷却期间不重新索敌并主动远离上一个目标。普通敌人的目标选择仍支持主机玩家和存活 AI actor，目标每 `TOY_GAME_RETARGET_MS` 重新评估。

西侧走廊出口旁墙面的 `button_west_corridor_no_tank` 可一次生成 16 个随机敌人，随机池包含普通、重型、快速、Smoker 和 Charger，不包含 Tank。

开发者区东南侧的 `button_enemy_death_test` 使用现有 typed horde spawn 在固定空地生成 Common、
Fast、Heavy 各两个，并逐个调用正式 reported-hit 致死入口。该按钮只负责构造可重复测试场景，不
绕过敌人 HP、击杀事件、`enemies_alive` 或 `dying_ms`；死亡展示仍由 effects 从 gameplay 状态派生。

敌人的主机玩家和 AI/远端 actor 候选都必须通过战斗区域检查。所有启用中的 `air_gate*` primitive
共同定义一条不可跨越的索敌控制线；即使正式地图的中央开发者门和上墙坡道让两侧仍属于同一导航
component，敌人也不能索敌到控制线另一侧。关闭整组空气墙后该限制随之解除。标记为
`developer_only` 的展示/测试角色不参与战斗索敌；普通地形造成的保守导航 component 分裂不用于淘汰目标。

普通敌人状态不再写入网络快照；快照只同步位置、朝向、生命、受击/死亡展示计时和特感能力状态。对应的显式编码入口是 `src/rasterfall_net.c` 的 `encode_enemy` / `decode_enemy`，修改敌人展示字段时要同步检查这里。

敌人 Content ID 当前按敌人目录顺序固定为 `PURSUIT_COMMON=0`、`PURSUIT_HEAVY=1`、`PURSUIT_FAST=2`、`SMOKER=3`、`CHARGER=4`、`TANK=5`。这些 ID 用于网络/内容身份；本地 `toy_game_enemy_type` 仍是数组索引，模型或渲染 profile 不应复用这组身份值。

开发者区域武器桌的 pickup 由 `src/rasterfall_session.c` 按地图坐标识别，直接解锁并装备；普通
商店和其他区域的武器 pickup 仍经过 `toy_game_weapon_unlocked` 检查，购买流程不变。

射击射线和命中结果由会话/展示适配器转换为 `rasterfall_effect_event`；该事件只描述开火、
弹着点、受击或预留爆炸，不是玩法状态，也不加入快照。

炸弹与 Molotov 是世界投射物/燃烧区实体。它们携带 `owner_actor_id`，由规则层将爆炸、燃烧
伤害及击杀统计写回投掷 actor；渲染和网络只读取这些世界实体状态。

## 跨层检查

游戏事件同时被音频、特效和网络消费；增加事件时搜索 `TOY_GAME_EVENT_`。结构或枚举若进入网络包，
不要直接依赖 C 布局，需在 `rasterfall_net.c` 显式编码并考虑协议兼容。新增地图实体通常要同时完成
解析、绑定、玩法交互、渲染和测试五处。
