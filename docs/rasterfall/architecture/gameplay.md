# 玩法、会话、地图与 AI

> 状态：当前

> 所有者：Rasterfall 玩法、session 与 AI

本文记录玩法真值及 session 的所有权。地图格式与碰撞来源见[地图与世界内容](maps-and-world-content.md)，角色资源与 LOD 选择见[角色表现](character-presentation.md)。

共享等级与精通的确定性能力映射由 `lib/game_combat.inc` 拥有，规则见[共享战斗能力](combat.md)。
actor 的等级、个人精通和生命进入主机快照；动作资源和渲染状态不进入该能力层。

[战术策略与实验](tactical-ai.md)使用同一个 `toy_game`：session 创建正式 actor，
策略输出只读观察对应的有界意图，Game 唯一推进角色。靶场和无图形训练也共享此路径。
控制来源、事件测量、预测隔离和旧模型退役边界由战术架构维护。

## 整数方向数值合同

`rf_numeric.h` 的公共接口由 `lib/game_numeric.inc` 实现，随现有 `game.c` 编译，
Game、session 和 hosted 战术工具使用同一整数算法。方向、速度、距离阈值与运动积分是不同用途，
不能统一替换成同一种舍入；通用 `isqrt()`、速度的保守限幅和运动余数语义不变。

`rf_direction_q10` 接受二维位移，每个分量的绝对值至多为 `2147483647`；差值必须在相减前
提升到 64 位。接口返回成功、零位移或非法输入；后两者不改输出。调用方零位移保留有效旧朝向，
新实例显式使用 +Z（`0,1024`）。支持输入与输出复用，禁止把超范围非零输入当作零位移。

小输入按二的幂扩展，使最大分量至少为 `2^29`，再求 64 位整数平方根；最终按绝对值最近舍入，
恢复符号。大输入不扩展。两个最大合法分量的平方和及乘除中间值均在 signed 64-bit 内。
平方根截断造成的归一化分量误差小于 `0.000002`，最终舍入至多增加 `0.5`；
合同保守采用分量误差小于 1、长度在 `[1022,1026]` 的上界，不要求整数长度恰好等于 1024。

枪手目标/逐步转向、感染体实际移动朝向、命中靶朝向与托管镜头最终对准共用此接口。
`actor command` 的 aim 字段是 Q10 方向，而非任意位移：长度须在 `[1014,1034]` 内，
非法命令记录原值并保留旧方向，合法值再归一化。此容差约为 1%，只属于方向输入合同，
不替代渲染矩阵校验或俯仰斜率合同。权威方向继续使用整数，不依赖浮点舍入或诊断开关。

生产者在读取异常旧朝向时记录来源和实体身份；默认确定性纠正可恢复值，严格诊断模式拒绝。
展示侧独立保证旋转基有效，见[角色表现](character-presentation.md#展示方向与旋转保护)。
异常诊断的缓冲、限频与启用方式见[数值异常定位](../guides/gpu-scene-fixture.md#数值异常定位)。

## 启动玩法配置

`rasterfall/config/gameplay.cfg` 提供波次、基地回血、推搡、药丸治疗距离、斧头、投掷物和回避的
可编辑参数，见[玩法配置](../guides/gameplay-config.md)。启动层只负责磁盘读取，
`game_gameplay_config.inc` 以纯解析器完成整份验证；`toy_gameplay_fields.inc` 统一声明字段、内置默认值
及范围。Game 的 `gameplay_config` 保存当前实例规则，不使用可变全局参数或文件 I/O。
session 保存启动配置，在 normal/legacy 换图及重试后、世界 actor 创建前应用到 Game，
同时初始化首波与回血计时；基地出生生命和 HUD 总波数均读取该规则。
独立 Game fixture 仍从内置默认值初始化。数组容量、内容 ID、协议与碰撞/导航结构常量保持编译期定义。

## World Content V1

空间地图（Spatial Map）remains authoritative for terrain, collision and neutral spatial
data. Rasterfall Game keeps a small `rasterfall_world_content` policy in the
session: Outpost builds Null and station content, while Campaign 01 builds
the campaign roster, content-defined Maid/Hurd formations, flags and support actors. Campaign-only
actors are not created in Outpost gameplay state. The policy is Game-owned and
is rebuilt when a world loads; it is not part of RF Core or Map Runtime.

Campaign Content 创建的普通队友 Jesus 现在携带 `RASTERFALL_CHARACTER_RF_RIFLEMAN` 稳定视觉身份。`toy_game_actor`
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

两套正式小队按 Campaign Content 的成员出生位置独立部署，队友不再绑定旗帜。
`RESP`、`ASLT` 和其他旗帜保留原物件位置、索引与携带用途，搬动旗帜不会改变任何队员的目的地。
RTS 编组是可重叠的 runtime 选择集合，人数覆盖整个 actor 池；正式 roster 身份不随玩家编组变化。
个体命令、选择与 HUD 的边界见 [RTS 核心指挥](rts-command.md)。

## 三层职责

`lib/game.c` + `include/toy_game.h` 是确定性玩法核心：武器/敌人定义、移动碰撞、地面与坡道、
射击和投射物、波次、事件、角色和敌人 AI、导航等。这里不依赖窗口或渲染器，适合最小逻辑回归。
`toy_game_update_profile` 是调用方临时持有的可选诊断结果与时钟入口；Game 只在设置该指针时
记录 world 更新分段和查询次数，时钟结果不参与玩法决策。runtime 每帧绑定并在逻辑更新后清除。

### 敌人共享目标导航场

默认敌人导航实现在 `lib/game_navigation.inc`，由 `game.c` 在移动/碰撞规则之后包含。
导航图、方向场、搜索堆、连接缓存与调度游标均归 Game；主机推进，渲染和网络快照不持有这些状态。
普通追击与特感非技能移动共用显式的普通敌人导航半径；冲锋等技能保留自身碰撞规则。
不再建立集团、等待集结、委派领队或修复成员到领队路线的接入。

底图保留旧 component 网格给队友和诊断路径使用；敌人另建实际通行尺度的节点图。同一水平格可以
保存多个支撑高度，节点保存实际 X/Z/Y；墙角、坡道端点及平台入口补充偏离格中心的节点。
有向连接由玩法移动规则扫掠验证，并缓存允许/阻挡结果。反向搜索检查的是前驱到当前节点的实际
前进方向，不假定坡道、落差和上下层连接对称。地图重建清除节点、方向场、连接缓存及个体引用。

每个目标 actor 身份对应一份共享导航场。固定容量缓存保存正边权代价、后继节点和可续算堆；
为降低无关区域的冷启动开销，队列使用朝首个请求位置偏置的优先级，不承诺最短路线。
所有当前接入请求被覆盖后暂停扩展，保留边界供其他位置的敌人继续请求。后继仅指向已确定节点，
沿路线代价严格下降。个体直接读取附近同层节点的共享后继，不再逐敌扫掠整段接入路线或等待接入时隙。
已找到后继、尚未出堆的节点也可提供方向；实际每步仍经过正式地面与身体碰撞。
保持局部路点直到到达，执行途中不被反复直达检查打断；没有路点时才尝试近距离直达。
端点高度必须与目标支撑面一致，同 X/Z 不同层不能借普通近战判定跳过导航。

持续移动优先：没有可用场、场正在清理或搜索未覆盖时，按目标大致方向尝试实际移动；受阻后按个体
固定绕行侧转向，并保持短时方向，之后再尝试目标方向。它是有界的局部探索，不保证走出任意迷宫。
每份场保存固定容量的格/高度指路记录，只有个体实际到达路点后才发布起始格到该节点的建议。
其他个体可直接采用建议；它不是整格可达证明，受阻由个体拒绝列表纠正，哈希冲突只丢失建议。
首版复用已有分层格和节点作为区域/出口，没有另建大区域门户图。

目标在局部移动时低频验证现有锚点到新目标的连接；连接仍成立即保留方向场，否则重新排队。
目标死亡、actor 身份变化和地图重建失效缓存；个体换场或场 generation 改变时清除旧节点链接。
缓存满时保留有待完成请求的有效任务，并通过最短租期与使用时间轮换其他条目；没有可用条目时继续
局部探索，不启动逐敌完整寻路。同槽场刷新保留当前物理路点，但清除旧节点链接；地图重建清除路点。
技能状态机仍拥有前摇、执行和冷却，导航不会覆盖技能运动。

搜索、初始化和连接扫掠采用固定工作配额，目标连接/直达有独立额度；缺少配额只推迟规划，不阻止移动。
个体长期接近不了路点时只记录本地失败候选，不立即触发全场修复。附近缺少节点或搜索已耗尽且无可用
方向时，才按所在格合并局部细分请求，每步生成一个候选点，完成一批后更新共享场。细分容量固定，
预算或容量耗尽不会调用旧 BFS，也不会把未知通路标记成已证明不可达；未表达的通路仍需补地图回归。

分离使用空间桶及有界邻居候选，同层敌人合并推力后只验证一次位移，推力不超过普通移速四分之一。
分离在接缝处保守拒绝，爬坡由普通移动负责；不会因拥堵直接阻断静态图边。
导航连接扫掠先生成覆盖整段圆形足迹的保守图元候选，按原图元顺序复用地面与碰撞查询；候选只在
该次同步扫掠中有效，返回前清除。单一平地完整覆盖且无相关碰撞的走廊可直接证明连通；其他情况
仍逐步使用正式运动规则。测试参考模式关闭这些加速，逐步比较完整 Game 状态与事件。
这只缩小导航扫掠的图元集合，不改变实际移动的全局碰撞语义。
初始建图与显式地图重建单独计量，不属于日常共享场扩展配额。

`RF_GAME_LEGACY_FLOW_NAV=1` 在 runtime 选择旧集团路径作同包对照；
`RF_GAME_LEGACY_GROUP_NAV=1` 选择更早的逐敌路径。环境变量只由 runtime 读取。
旧集团回归显式关闭共享场；默认行为回归覆盖共享复用、窄门、上下层、容量、公平轮换、断路重建和确定性。
`SCENE-LOGIC-FLOW` 报告共享工作、等待与修复，具体采样见[性能诊断](../guides/rendering-performance.md)。
时钟只用于诊断，任务优先级、配额和轮换不读取耗时或消耗战斗随机流。

### 普通角色直达规划缓存

普通友军、枪手和离线 RTS 本地玩家共用个体路点规划。`toy_game_actor_navigation_target()`
只更新角色导航意图并返回转向点，不推进身体、武器、随机流、事件或世界时钟；实际动作仍归原调用者。
已证明可直达的规划结果最多复用 256ms 固定步时间，
停止在 RTS 岗位也消耗期限；负结果不缓存。目标坐标、支撑高度、地图导航代际变化或实际移动受阻
立即失效，地图重建清除路点与缓存。新 RTS 命令通过 `toy_game_actor_cancel_navigation()` 清除旧意图。
缓存只减少路线查询，每一步仍使用正常角色碰撞和地面支撑规则，不复用碰撞响应。
`actor_direct_queries/hits/blocked` 记录重查、命中与阻挡，诊断时钟不影响缓存期限或动作。

导航线段采样必须同时检查 `has_support`；有限道路外查询的缺省零高度不代表存在地面。
分层连接的起点若身体仍落地、中心却只有部分当前层支撑，允许按正式身体运动回到该层；
连接逐步检查身体未转入空中，中心首次取得当前层支撑后，不再允许经过新的无支撑中心。
这只恢复已落地边缘起点，不放宽一般节点之间的缺口或跨层连接。

保守整格导航不能把角色端点的格子阻塞当作实际身体不可达。普通角色在端点格子被挡时，
只在 2400 RFU 内查找最近可连接的格中心，用角色真实半径、完整线段碰撞和支撑高度验证连接；
目标连接须属于起点的同一导航 component。搜索仍使用既有有界格图与路点，端点连接不放宽
整格图、实际移动碰撞或旧感染者路径。稳定回归覆盖五人、窄门、端点旁障碍、关门和重开。
若角色端点的直线连接失败且目标 component 已知，只尝试 2400 RFU 内最近四个同层格中心的
两种轴向拐点，最多十六次短线段探测，不增加 BFS。两段均验证真实支撑、身体与高度；
拐点及出口还用包含现有临时路点到达容差的更宽查询验证，不能提前切段穿墙。
该修复只进入失败的端点连接路径，正常直线、缓存与成功连接不增加探测。


AI 朝向是长度约为 1024 的整数单位向量。`ai_turn_toward` 对短移动位移先扩展精度，
再求整数长度及归一化，包括初始朝向和最终对准目标分支；不能直接除以向下取整的 RFU 距离。
否则短斜向移动会产生偏长朝向，并在身体、挂点和装备的世界变换中表现为缩放。

### 可选普通角色分队

Game 的 `game_actor_squad.inc` 保存最多五组、每组五名普通 actor 的自动移动意图。
它与 runtime 可重叠的 RTS 编组独立：session 按稳定 actor index、ID 和 combat generation
明确绑定成员；本地玩家只作 FOLLOW 目标，不是成员。首图绑定五名友军跟随玩家，以及两批各三名
增援枪手进攻庭院；两组自动集结锚点分别偏移庭院中心 X 轴 ±1080 RFU，actor 原部署点不变。
十二名原守军继续各自岗位规则，感染者仍使用独立追击和共享导航场。
部分组可追加后续成功生成者，分队分配失败保留原 actor 行为，不参与任务配额或胜利统计。

显式 MOVE/STOP 优先于救援，已认领救援优先于自动分队，随后才是原副官跟随或部署。
枪手只在无战斗目标和最后已知目标的空闲进攻中使用分队落点；战斗追击、退让、守区和控制约束
继续由普通 AI 决策。分队不写 `command_destination_active/command_x/command_z`。
STOP 和到达后保留的显式目标持续屏蔽自动重排，单人的命令也不拖动其余成员。

成员倒地保留身份与槽位，存活领队失效后才按稳定顺序接替，并交换中心与新领队原来的槽位；
复活的旧领队不会抢回中心。身份复用、阵营改变或正式死亡剔除旧引用，地图导航重建清分队。
FOLLOW 锚点也必须持续存活、身份有效且同阵营；失效时停止发布移动意图。
分队 handle 使用自身代际，在同一 Game 生命周期内 reset/release 后失效；完整 `toy_game_init`
开始新 Game 生命周期，session/world owner 必须同时丢弃全部旧 handle，不能跨世界只验证裸组号。

自动落点使用 720 RFU 间距；实际支撑、身体净空和有界短连接检查通过后才缓存。
局部侧向落点或最终落点受阻时以领队真实移动的固定容量轨迹进入纵列。
纵列目标刷新与完整的局部、最终阵形检查交替进行，两次完整检查均可用后才展开，
避免到达容差内局部净空可用、最终槽位仍撞墙时反复切换。
拒绝的落点保留当前地图/高度下已验证的旧目标，缺少有效轨迹时暂不发布新目标。
FOLLOW 在绑定时初始化方向，之后只由目标实际 X/Z 位移达到 256 RFU 更新行进方向；
原地转头、瞄准和射击朝向不使队员绕目标旋转。每名角色仍调用既有个体规划和物理碰撞。

每个正常 AI 固定步最多 25 次成员、25 次 resolver 和 5 次 FOLLOW 目标身份检查，合计 55；
稳定游标最多刷新两组，合计最多十次候选检查，每组刷新间隔至少 256 逻辑毫秒。
候选检查由既有支撑/身体/短连接查询组成，不建立额外 BFS；这些查询内部的碰撞扫描成本须另行测量。
`squad_profile` 只记录检查、刷新、探测、目标/领队/纵列切换，查询可增加诊断计数，不修改移动真值。
暂停与加载不推进这份调度，网络客户端不执行第二份分队权威模拟。

### Session 与 AI 接口

`src/rasterfall_session.c` + `include/rasterfall_session.h` 是模式编排层：加载/重置关卡，构建和执行
玩家命令，剧情阶段、托管角色，以及主机/客户端不同的 step/replay 路径。
离线 RTS 命令由 session 校验：本地玩家目标保留在 session，队友的独立目标写入 Game actor 的
`command_destination_active/command_x/command_z`，在正常 AI 固定步内优先于出生部署和副官跟随。
本地玩家的 RTS 最终目的地仍由 session 保存；session 查询 Game 转向点，向现有玩家移动路径提供
前进/侧移输入，并独立保留敌人瞄准和射击。临时路点采用小于最终目的地的输入死区，不能代替最终到达判断。
实际身体每步只移动一次，碰撞偏离请求位移时清规划重新查询；新命令、STOP、合法传送和 FPS/RTS
切换清除本地规划；空中、特殊控制或剧情 hold 也清除规划并暂停 RTS 移动输入。
FPS 手操接管身体时保留既有目的地，恢复 RTS 后从当前位置重新规划。
移动、索敌、射击和动画仍通过正式 actor API 推进；旗帜不写入部署点。
镜头、框选、编组和高亮属于 Game Runtime 展示状态，不进入 `toy_game`。当前 RTS 不接入联机命令协议。

`src/rasterfall_ai.c` + `include/rasterfall_ai.h` 管理可插拔 AI 注册表，把 observation 交给控制器并
将 decision 同步回游戏；具体内建战斗和移动规则大量仍在 `lib/game.c` 与 session 的托管 AI 中。

本地玩家现在是 `toy_game.actors[TOY_GAME_PLAYER_ACTOR_INDEX]`（当前为槽位 0）的正式
`TOY_GAME_ACTOR_PLAYER`；远端人类也占用固定 actor 槽位。位置、生命、空中状态、库存/武器计时、
统计、动画、倒地/复活和特殊控制均持久化在 actor 中。
正式世界规则、session、HUD 和展示层都直接读取 actor；不存在独立的玩家状态副本或仅供玩家使用的
规则入口。session 的本地 step 与 client prediction 已直接操作 actor 的
移动、跳跃、airborne、武器/reload/fire cooldown、投掷物、animation 和 special-control；camera.body
始终由 actor 派生。AI 分配从槽位 1 开始，网络协议和远端 actor 槽位不变。

session 的救援、交互死亡判断和托管武器决策读取 actor 状态；出生点和正式 world step
直接写入或推进 local actor。

玩法不维护金钱、击杀/波次收入或购买解锁状态。旧商店、AI 雇佣/付费升级/付费换枪、买旗、
付费复活及加钱/清除雇佣兵按钮和命令已移除。普通武器拾取直接装备，托管武器准备路线读取
实际武器拾取点；制造仍使用网格编织机的独立材料资源。波次导演使用独立敌人权重，保持原有难度预算。

## 边缘站点 01 的局部任务权威

`rasterfall_session.frontier` 唯一持有首个边缘站点的阶段、设施接管和生成配额；
`lib/rf_frontier_mission.c` 实现局部导演，`src/rf_frontier_session.inc` 负责地图绑定、
正式队伍、设施权限和失败决策。该任务只开放离线会话，使用普通 actor、感染者、战斗与导航规则，
不建立第二套角色或敌人模拟。`toy_game.external_director` 停止既有自动波次导演，世界战斗和制造仍正常推进。

session 在 Game 完成权威固定步后调用任务 step；加载、预热、暂停、字幕和视角切换都不推进任务时钟。
初始守军肃清的同一步先进入整备，再考虑周期生成。周期感染者只统计本任务的周期来源，
超出活体上限或缺少一整波的容量时跳过机会，不保留补刷债务；已创建的感染者跨阶段保留。
最终感染者和枪手按累计成功创建配额计数，击杀不补额；容量不足保留未完成配额，
感染者池与 actor 池分别采用有界重试。具体组成与时序见[站点任务合同](../reference/frontier-station-01-task.md)。

任务记录携带 mission ID、spawn role、序号、actor ID 或 enemy 索引以及 combat generation。
槽位只用于定位，身份与 generation 必须同时匹配；全局击杀数、展示尸体和其他内容对象不参与清剿统计。
胜利要求固定配额成功创建完毕、无待入场敌人、任务活体归零且三设施全部接管，事件只发布一次。
玩家倒地沿用队友救援规则，不能仅因玩家暂时倒地宣告失败。重试和换图重建任务计时、队列、身份与接管状态。

接管请求由 session 重新核对设施 authored ID、设备 identity/world generation、执行 actor 身份与代际、
存活状态、交互距离和任务阶段；UI 禁用只提供反馈。车间控制制造使用权，能源控制供电和 X1，
仓库控制 CPU 与固定存储容量。未接管仓库时存储能力禁用；首次接管只开放配置容量，不补材料或能源。
接管幂等，产权标记与运行开关分离，反复开关或重新发送请求不会授予第二份库存。
制造、成品领取和装备继续由既有 `toy_game.weaver` 与 actor 规则拥有；制造与开机都不是胜利前置。

## Hurd Relay gameplay foundation

session reset 按 Campaign Content 恢复四名 `ANIME_GUARD_*` Maid 队员，保留
`anime_character_id=2..5`、出生坐标与 AK，通过 actor 的
`character_id=RASTERFALL_CHARACTER_MAID` 解析为 Maid profession。Maid 和 Hurd 的旗帜仍保留各自索引，
所有 session 队员统一清除 `flag_index/flag_guard`，随后接受独立 RTS 命令。

session reset 在正式 world 中创建固定 Gunsmith、Logistics、Medic、Guard 四名普通 AI actor，并明确
写入四个 Hurd 专用稳定 `character_id`；前三人初始使用 Pistol，Guard 使用 SMG。普通 player、地图佣兵
以及 Eula actor 使用 `RASTERFALL_CHARACTER_NONE`，不按 actor slot 或 class 随机
选择 Hurd identity。四人的移动、部署、防守、战斗、受伤、
DOWNED、REVIVE、动画和武器仍走普通 actor 规则。`hurd_outpost.squad_actor_indices[]` 定位固定角色，
也作为 Hurd 人员统计来源；不再依赖旗帜 assignment。

北侧 HURD 旗帜初始部署于 `(0, 28500)`，固定 control region 为
`x=-5000..5000, z=25500..31500` RFU。`rasterfall_session_hurd_status()` 是无副作用派生查询：旗帜必须
active、未被携带且位于区域内；assigned count 统计 Hurd roster 中的有效 actor，capable count 只统计
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
去重；来自不同 Charger 的撞击不受玩家击飞冷却阻挡。Tank 的挥击也强制刷新击飞，不被玩家击飞冷却
吞掉。Charger 和 Tank 的击飞水平目标距离分别为 9m 和 6m；命中后的世界空间抛物线轨迹由展示层保留
3000ms，不进入玩法真值或网络快照。

## 常态单位体积碰撞

`lib/game_unit_collision.inc` 拥有动态身体查询与接触滑行；`game.c` 保留地图、支撑高度和
特殊运动的所有权。玩家、普通队友、敌方枪手及存活感染者的常态移动互相阻挡，不按阵营穿透。
倒地、死亡、空槽和动画展示 actor 不参与；基地核心由原有设施/地图规则表达，不作为普通人形身体。
静止和暂停模拟的存活单位仍是阻挡体，正常接触不推走对方。

身体使用水平圆形和脚底到 `RASTERFALL_HUMAN_HEIGHT_RFU` 的垂直区间。actor 采用既有玩家
半径，感染者采用各自类型半径；模型、骨骼、命中半径和动作不改变通行体积。
脚底为 `ground_y + airborne_y`，只有高度区间重叠的扫掠时段才阻挡，不能只按楼层编号或脚底差判断。
正常跳跃的水平运动也检查动态身体；单位不提供站立顶面，垂直支撑仍由地图负责。

真实池成员通过整数扫掠查找最早接触，截断前进并保留接触切向位移。地面 actor 的长位移按半径
一半分段，每段最多三次接触响应；每次实际提交仍检查地形与动态身体，不能因迭代耗尽放行。
接触留少量 RFU 余量以覆盖整数取整。玩家地面惯性在单位接触后保留实际接受的运动，避免圆形
滑行同时改变两轴时被当成两面墙清零。已有重叠允许沿不加深重叠的方向脱离；没有自动把静止
人群推散的第二套位置求解，也不能通过墙体脱离。出生、传送和特殊技能仍需各自保证合理落点。

动态查询扫描固定容量的 actor/enemy 池，先按扫掠 AABB 排除远处身体，再做精确查询。
不保存容易因换图、传送和直接位置写入失效的空间索引，不截断真实碰撞候选，不分配内存或使用
战斗随机流。此实现的成本须按实际密集场景测量，不能将固定池容量当作性能签收。
感染者原有软分离保留，但最终位移同样检查动态身体；普通咬人距离至少覆盖双方身体半径与
接触余量，避免有碰撞后站在接触处无法攻击。玩家被咬后保留伤害、咬击冷却、受伤反馈和 BITE
事件，不再自动反推或击飞感染者，也不消耗玩家的击飞冷却；显式推击和特殊冲击仍按原规则处理。

`toy_game_position_blocked_at_height()`、`toy_game_short_connection()` 和静态导航证明仍只消费
地图。借用脱离 Game 池的临时 actor/enemy 探测地形不会被其他单位阻挡；实际 Game 深副本中的
正式池成员仍有相同碰撞。因此普通游戏、靶场、策略对抗、CLI 和隔离预测共用身体规则。
AI 遇到单位阻挡时保留已有静态路点，移动偏离请求时重新验证直达缓存；完全受阻时尝试
确定性的局部侧移。侧移也受真实碰撞约束，窄处没有空间时等待，不保证任意拥堵都能自行解开。
没有已证明可通行的路线时等待重新规划，不直接向最终目标盲走。地图阻挡仍按原规则清理规划。
感染者将单位阻挡与地形停滞分开记录，排队不触发地形路线修复。默认流场在单位阻挡时可用既有
局部预算验证到后继节点的静态通路，再跳过拥堵路点；验证按单位错峰，未实际到达的节点不发布
共享到达提示。终点锚点被占据时，验证当前位置到目标的通路后释放锚点。预算不足或通路不成立时
保留原路点，每步移动仍检查身体。旧集团导航允许绕开被身体占据的
路点，但新接入段必须通过静态地形检查；偏离格中心时使用端点接入。64 单位旧走廊回归分别限制
共享寻路次数与局部接入次数，后者限制整次通过的总数不超过单位数的三倍，不能作为当前流场的性能结论。

Smoker 控制、击飞和 Charger 冲锋保留特殊运动策略，不因普通碰撞提前截断技能命中。
联机协议、移动归属与校正不在此次改动的开发和验证范围内。

## 玩家地面移动与跳跃

`toy_game_move_player_input()` 拥有玩家输入运动：方向先归一化，再按移动能力倍率确定目标速度，
地面以有界加速度接近目标；松键以独立减速度停止。速度与不足一个 RFU 的位移余量保存在
actor 的 `move_velocity_x/z`、`move_remainder_x/z` 中，不放进相机或纯展示状态。
速度使用每个 60 Hz 逻辑步的 1/1024 RFU；加减速由米制参数转换，避免短按或斜走丢失小数。
基准移动约 8.91m/s，地面加速度 60m/s²，松键减速度 75m/s²；与当前速度反向的输入使用
90m/s² 加速度，减少左右切换的拖滞。这些是内置默认值；运行配置入口为
`rasterfall/config/player-movement.cfg`，使用方法见[玩家移动配置](../guides/player-movement-config.md)。
启动层读取磁盘配置，`game_player_movement.inc` 纯解析器验证并换算为 60Hz 参数；Game 的
`player_movement` 持有当前规则，不执行文件 I/O。session 保留启动 policy 并在地图加载、legacy 加载、
换图和重试后重新应用；本地 FPS、RTS 与客户端预测使用同一规则，独立 Game fixture 仍从内置默认值初始化。
身体碰撞复用坡道与滑墙规则，受阻轴清除速度和余量；传送、复活、击飞及禁止移动清除地面惯性。
RTS 明确 STOP 或到达最终目的地立即清除地面速度，手动松键仍按减速度制动。

起跳命令只捕获世界方向，供相机朝向变化后的客户端 replay 使用；水平初速度来自实际地面速度。
actor 的 `jump_coyote_steps` 和 `jump_buffer_steps` 保存配置指定的 60Hz 输入步数（默认各六步，约 100ms）的
离边起跳宽限和落地前输入缓冲。离边跳从实际脚底高度起跳，消耗宽限，不允许主动跳跃后再次起跳；
缓冲在下一次落地后的输入步消费，不在落地扫掠中额外推进身体。松开方向键也逐步消耗期限，
暂停不消耗；成功起跳、禁止控制、剧情 hold、击飞、传送和复活清除容错状态。
起跳或走出平台后，空中由 Game 的同一运动步骤推进惯性、重力和碰撞；session 不额外叠加输入位移。
普通跳跃/掉落允许按键以 10m/s² 微调水平速度，松键保留惯性；方向先归一化，总水平速度受相同的
移动能力上限约束。输入步骤只修改 `air_x/z` 和其 1/1024 RFU 速度余量，不改位置或垂直速度，
实际位移仍只由原有空中扫掠推进。撞墙时清除受阻轴的速度及小数余量，不能在墙边积累额外速度。
特殊攻击禁用控制和剧情移动 hold 时不接受空中微调，击飞继续由原有规则控制。
地面步骤已经完成水平位移的离边帧设置 `air_skip_horizontal_step`，该帧空中步骤只推进垂直运动，
下一帧再用保留的水平速度推进，避免交接时重复移动；起跳不先执行地面位移。
普通玩家跳跃/掉落使用约 21.09m/s² 的重力与约 40m/s 的下落速度上限；当前离散积分的平地
跳跃峰高约 1.40m、滞空约 0.73s，默认能力满速平地跳远约 6.53m。运动回归保护 1.3～1.5m
峰高和 6～7m 平地跳远包络，并用独立几何验证高台可达性，不锁定正式地图的易变坐标。
受特殊攻击控制的击飞及非玩家运动使用独立的原有重力和速度上限，
击飞目标距离的初速度换算也使用该重力。正常落地保留未受阻方向的水平惯性及速度余量，随后由地面输入加速或制动。

离线和客户端预测使用相同规则，每个空中子步先检查头顶和身体碰撞，再检查局部坡面或平台的落地接触，
首次落地即结束空中扫掠，不能高速穿过整段楼梯后才在终点查找地面。现有可信客户端位置报告协议保持
由客户端推进地面运动，主机不再模拟第二份输入加速度；地面速度无需追加到远端展示快照。

## 地图链路

坡道与平台的连续性允许平台覆盖坡道高端并向高端外侧延伸，适用于正式地图
开发坡道与空气墙顶面的既有重叠布局。仍需中心位于坡道、有效坡面支撑和不超过
正常 step 的端点高差；侧向攀爬、真正高度断层及间隙保持阻挡。具体判断在
`game.c` 的 `ground_has_ramp_surface_transition()`，不从视觉 prop 推导。

V1 `object` 环境组合投影到静态实例；显式 component collision 在 Runtime Map 展开为独立 collision，
与既有 collision records 一起投影到玩法。
Legacy `prop` 文本的 profile 默认碰撞规则不等于 V1 object 规则，详见 reference/map-format.md。

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
- 剧情与托管玩法：`rasterfall_session.c` 的 `campaign`、`managed_ai` 区域。
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

所有区域的普通武器 pickup 由 `src/rasterfall_session.c` 直接装备，不依赖购买或地图坐标特例。

射击射线和命中结果由会话/展示适配器转换为 `rasterfall_effect_event`；该事件只描述开火、
弹着点、受击或预留爆炸，不是玩法状态，也不加入快照。

炸弹与 Molotov 是世界投射物/燃烧区实体。它们携带 `owner_actor_id`，由规则层将爆炸、燃烧
伤害及击杀统计写回投掷 actor；渲染和网络只读取这些世界实体状态。

## 跨层检查

游戏事件同时被音频、特效和网络消费；增加事件时搜索 `TOY_GAME_EVENT_`。结构或枚举若进入网络包，
不要直接依赖 C 布局，需在 `rasterfall_net.c` 显式编码并考虑协议兼容。新增地图实体通常要同时完成
解析、绑定、玩法交互、渲染和测试五处。

## Component collision 高度区间

生成组件碰撞保留 base_y 和顶部高度；水平 body query 允许从架空体下方通过，ground query
不会把架空盒顶当成下方地面。上升运动对架空体底部做头部扫掠并限制高度，local/remote
player、AI actor 与 enemy 共用 clamp。当前 body 高度统一使用 1750 mm 契约，未细分特感身高。
现有 2D perception/fire/auto-aim 查询按源眼高排除更高的架空体，仍不等价于完整 3D hitscan。
外围 boundary 模式保留独立 BLOCKS_AIRBORNE，普通设备允许从顶部高度越过。

落地由 Game 在旧位置与本步新位置之间比较脚底和各自局部支撑面的相对高度，选择穿过的最高可站立顶面；坡面不能只用终点高度与旧脚底高度比较，也不能仅在垂直速度向下时检查，跳跃最高点附近的水平移动仍可能迎面穿过升高的坡面。
不能从盒体或坡面下方吸附到它的顶部，也不能跳过薄板。local/remote player、AI actor 和 enemy 共用该规则。
普通角色仅在实际局部坡面高度与当前脚底高度相差不超过跨步高度时保持楼梯支撑；不能把坡道整个端点高度区间当作当前位置的接触。完整足迹下方虽有更低楼板，局部楼梯接触仍须保留。平台离边后的大落差进入空中状态并保持原绝对高度，由落地扫掠接住下方楼梯，不能瞬间吸附到低一层的坡面。
支撑重叠与落地使用圆形足迹，排除矩形角外的虚假支撑；水平身体碰撞、头部扫掠和导航的
完整足迹支撑仍采用保守覆盖。窄护墙顶允许部分足迹落地和保持支撑，离开实际支撑边缘后进入下落。
设备顶面由独立 component 模板显式标记 walkable；椅子座面/靠背和终端底座/立柱/屏幕/托板
分别拥有高度区间。boundary 空气墙和不应落脚的装饰仍保留各自声明，不读取模型 AABB。

## 网格编织机制造权威

普通机实验原型的任务、有限能源、实际算力分配与待取成品由 `toy_game.weaver` 唯一拥有；
`rasterfall/lib/game_mesh_weaver.inc` 在 `toy_game_update_world()` 的固定逻辑步中推进，
`toy_mesh_weaver.h` 声明纯数值输入和接口。离线前哨站与边缘站点 01 通过各自地图设备绑定开启，不进入客户端预测或
联机快照；接入联机前必须补充主机命令与完整状态快照，不能依靠本地展示时钟复制任务。

蓝图固定使用完整制造资产的拓扑顶点数、三角面数、米制实体体积和包围尺寸；离线生成目录
附来源及运行资产哈希，不读取相机 LOD。统一系数计算工作量与成型能量，初始标准弹匣按
现有武器容量另计每发工作量及能量。RF1 的 GHz 通过编织任务效率映射吞吐，X1 提供该任务
的附加吞吐；成型功率扣除机器和已开启计算设备的基础功率，有限能源只支付实际推进消耗。
校准和交付也支付基础功率。电源、CPU、X1 开关只改供给，不能补满储能或回退已耗工作。

机器一次持有一个任务，经历校准、编织、交付、待取；缺电、缺算力或缺存储冻结当前阶段，
恢复后续作。待取成品不再消耗制造资源，断电仍存在，并阻止新任务。session 只把此成品
投影到 `items[weaver_item_index]`，复用普通距离、朝向和 E 互动；设备表现从同一权威状态
绘制交付姿态，标准拾取模型跳过这一投影，避免重复显示。领取通过正式 actor 装备规则
替换对应槽位，获得一个标准弹匣及零备弹；后续射击、切枪和换弹走既有
规则。行动部补给为有限手枪补三个弹匣，旧出生手枪及地图装备的无限备弹行为保持原合同。

同一 world/session 内任务与成品持续存在，普通 session reset 或换图重建会清除它们。
当前没有跨退出存档接口，本原型不另建存档系统。机构朝向、叶瓣、光束和网格形成等状态
归只读表现，不进入 `toy_game`；演出读取统一任务进度，不反向决定产物生成或资源扣除。
