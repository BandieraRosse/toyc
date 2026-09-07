# 玩法、会话、地图与 AI

> 文档更新：2026-09-07
> 源码核对基线：工作区（所有人类玩家和 AI 的移动/跳跃/airborne/朝向/武器/库存/切枪/reload/动画/special-control/shove/统计均由 `toy_game_actor` 拥有；`toy_game_set_remote_actor()` 只负责远端 actor 生命周期；客户端展示缓存不参与玩法规则；投射物/燃烧区携带 actor owner）

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

敌人 AI 现在分为两条路径。`PURSUIT_COMMON`、`PURSUIT_HEAVY` 和 `PURSUIT_FAST` 只做最近有效目标选择、导航追击和攻击距离判定，不再经过视野方向、观察、警戒传播、枪声调查、丢失目标或搜索状态。`SMOKER`、`CHARGER`、`TANK` 仍进入各自的特感更新函数，并保留技能的独立索敌、前摇、冲锋/束缚/横扫逻辑。普通敌人的目标选择仍支持主机玩家和存活 AI actor，目标每 `TOY_GAME_RETARGET_MS` 重新评估。

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
