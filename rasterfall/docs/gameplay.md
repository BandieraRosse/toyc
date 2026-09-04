# 玩法、会话、地图与 AI

> 文档更新：2026-09-04
> 源码核对基线：工作区（开发者区武器 pickup 直接解锁/装备，effect event / presentation cue）

## 三层职责

`lib/game.c` + `include/toy_game.h` 是确定性玩法核心：武器/敌人定义、移动碰撞、地面与坡道、
射击和投射物、波次、事件、角色和敌人 AI、导航等。这里不依赖窗口或渲染器，适合最小逻辑回归。

`src/rasterfall_session.c` + `include/rasterfall_session.h` 是模式编排层：加载/重置关卡，构建和执行
玩家命令，商店与雇佣 AI，剧情阶段、托管角色，以及主机/客户端不同的 step/replay 路径。

`src/rasterfall_ai.c` + `include/rasterfall_ai.h` 管理可插拔 AI 注册表，把 observation 交给控制器并
将 decision 同步回游戏；具体内建战斗和移动规则大量仍在 `lib/game.c` 与 session 的托管 AI 中。

## 地图链路

- `include/toy_map.h`：磁盘地图解析后的通用结构。
- `lib/map.c`：文本 `.map` 解析器；新增语法或字段从这里开始。
- `include/rasterfall_map.h` / `src/rasterfall_map.c`：把地图绑定为玩法盒体、图元、可交互物和安全区。
- `assets/maps/rasterfall.map`：正式公开关卡数据。
- `rasterfall_render.c`：只负责把地图结构画出来；不可用视觉几何代替玩法碰撞。

## 按任务查找

- 武器数值、弹药、射速、价格：`toy_game.h` 的 weapon 枚举/结构和 `game.c` 的武器表与操作。
- 敌人类型、技能、波次：enemy 枚举/信息表、wave plan、enemy update 路径。
- 移动、坡道、跳跃、碰撞：`toy_game_query_ground`、`position_blocked`、motion/navigation 相关函数。
- 玩家输入产生何种动作：`rasterfall.c` 构造 command，session 执行，game 落实规则。
- 商店、剧情、队友雇佣、托管玩法：`rasterfall_session.c` 的 `shop`、`campaign`、`managed_ai` 区域。
- 角色外观选择：`rasterfall_character.c`；角色动作状态仍由 `toy_game_actor.animation` 等字段拥有。
- HUD 显示错误：先确认 `rasterfall_hud_state` 在主循环中是否正确填充，再改 `rasterfall_hud.c`。

开发者区域武器桌的 pickup 由 `src/rasterfall_session.c` 按地图坐标识别，直接解锁并装备；普通
商店和其他区域的武器 pickup 仍经过 `toy_game_weapon_unlocked` 检查，购买流程不变。

射击射线和命中结果由会话/展示适配器转换为 `rasterfall_effect_event`；该事件只描述开火、
弹着点、受击或预留爆炸，不是玩法状态，也不加入快照。

## 跨层检查

游戏事件同时被音频、特效和网络消费；增加事件时搜索 `TOY_GAME_EVENT_`。结构或枚举若进入网络包，
不要直接依赖 C 布局，需在 `rasterfall_net.c` 显式编码并考虑协议兼容。新增地图实体通常要同时完成
解析、绑定、玩法交互、渲染和测试五处。
