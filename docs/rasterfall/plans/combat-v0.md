# 战斗原型 V0

> 状态：活动；2026-10-03 用户授权长程实现
> 开始：2026-10-03 01:43:46 +08:00（Asia/Shanghai，系统 Get-Date）
> 基线：217963a55a0400b29d9424518034163ef15d03b0

## 目标与边界

落实根目录 `Rasterfall_Combat_V0_Astra_Ultra_Prompt.txt`：可玩的普通/精英枪手、共享五项能力、
连续回避、枪械与感染者平衡、组件模型、实验区固定测试和结构化结果。核心通过后再打磨叠加动作
与共享身体。约八小时为参考窗口，按系统时间记录；不等待凑时，不自动 push。
保留特感机制、玩家明确指挥和原有友伤政策，不扩展编译器、bootstrap、GPU/天空、制造或终端系统。

初始工作区仅有未跟踪的用户提示词；不把该文件混入实现提交。旧 GPU 光照计划暂停接续，成果保留。

## 本地核查

以下路径均以仓库根为起点；“已有”指源码核对，运行证据另列。

| 项目 | 已有与缺口 | 准确入口 |
| --- | --- | --- |
| 生命与伤害 | 玩家/队友均为 actor；感染者独立 enemy。普通咬伤分玩家/AI，Smoker 另扣血，冲击入口处理爆炸/Charger/Tank；枪弹只打 enemy。未发现护甲、真实部位倍率 | `rasterfall/include/toy_game.h`；`rasterfall/lib/game.c` 的 `bite_player`、`bite_ai`、`smoker_update`、`apply_entity_impact_with_knockback`、`toy_game_apply_reported_hit`、`fire_ray` |
| 队友战斗 | 找敌/遮挡/转向/移动在内建 AI，射击/弹药/换弹已共用 actor executor；每步扫描 enemy。玩家 200% 射速/60% 换弹、L3 AI 80% 间隔是旧例外，须收敛 | `toy_game_update_ai_teammate`、`toy_game_execute_actor_command`、`toy_game_actor_fire`；`rasterfall/src/rasterfall_ai.c` 控制器注册 |
| 阵营与友伤 | 没有通用阵营字段；枪弹忽略 actor，感染者选择玩家及存活 AI；特殊冲击会伤及人形。空气墙和 developer_only 限制索敌 | `enemy_target_valid`、`enemy_select_target`、`fire_ray`、`toy_game_explode` |
| 特感保护 | Smoker 每连接独立 4s 牵引/8s 冷却，近身每秒 2 伤害；多个连接仍由原状态机拥有。Charger 每轮 actor mask 去重，Tank 挥击强制击飞；不改控制设计 | `rasterfall/lib/game.c` 的 smoker/charger/tank 更新；`rasterfall/src/rasterfall_logic_test.inc` 特感回归 |
| 等级技能 | L1/L2/L3 AI 和升级商店已有；无五项精通。现有 CP 仅导演粗评分，保留而不新增评分体系 | `toy_game_ai_class`、`ai_table`、`toy_game_upgrade_ai`、`toy_game_actor_combat_power`；`rasterfall/include/toy_game_config.h` |
| 角色与动画 | canonical Humanoid V2.1 身体、recipe/gear、21-role/8 socket、独立 instance 已有；lower/upper/additive recoil 后执行 attachment IK。资源多为本地 private-assets，须闭合可重建与打包 | `tools/blender/generate_rasterfall_humanoid_v2.py`；`rasterfall/src/rasterfall_character.c`、`rasterfall_action.c`、`render/rf_gpu_scene_pose.inc` |
| 实验与统计 | Outpost 有展示终端和独立性能实验生命周期；展示 actor 不是真值。需要新增拥有自身对象的实际战斗预设，复用 UI/日志机制 | `rasterfall/src/rf_experiment_labs.inc`、`rf_performance_lab.inc`、`rasterfall_session.c`；`tools/gpu_performance_lab.ps1` |
| 游玩与构建 | Outpost 入口/指挥桌选择正式地图；FPS/RTS 单人命令归 session。Windows staged 运行目录与递归资源复制已存在 | `rasterfall/assets/maps/outpost.map`、`rasterfall.map`；`rf_outpost_table.inc`、`windows/NativeCodex.ps1`、`windows/Makefile` |

## 实施顺序与提交门槛

1. **已提交**：能力映射与代表模板；统一机械射速、换弹、散布、移动、耐力生命，兼容原等级升级。后续切片已消费回避与弱点；承载仅保留接口。
2. **实现并验证**：阵营、共享三维射线/结算和普通/精英枪手；队友命令与敌方决策分离，250ms/全局预算感知。
3. **实现并验证**：连续回避、攻击分类/聚合、恢复与事件、平衡、协议45；结算、特感、快照和表现去重回归通过。
4. **实现并验证**：共享组件模型、21个固定战斗预设、现场入口与CSV；双Suite复现、公开资源检查和规模实验通过。
5. **核心完成**：原生输入、GPU混战资源及旧64感染者对照通过；致死击飞、落地控制、统一复活入口及RTS队形修正均经最终构建与逻辑回归确认。
6. 核心通过后：独立提交叠加回避动作及共享身体/装备改善。

每个可构建切片运行最近回归、审阅 diff、`git diff --check` 后中文提交；只暂存任务文件。
当前参数在实现落地后由战斗架构文档和配置源码拥有，这里只保存阶段决定与现场结果。

## 时间与证据

| 时间（+08:00） | 事实、验证与下一步 |
| --- | --- |
| 01:43:46 | 开始，完整读取提示词、AGENTS 与维护路由；HEAD 和工作区已记录 |
| 01:46:48 | `powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 doctor` 通过；CIM 枚举受限但 Vulkan loader 可用。`test` 构建并完成全部基线逻辑回归，退出 0；副本 `tmp/combat-v0/baseline-logic.log` |
| 01:47 | 现有 FULL 场景 2（64 感染者）原生基线通过：`powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -OutputDirectory tmp/combat-v0/baseline-native -Rounds 1 -Scenes 2 -Stage Full`；506 帧，mean 15837µs / P95 19134µs / GPU 3.943ms，alive 始终 64 |
| 02:11 | 技能切片原生 build 与完整逻辑回归通过，真实进程退出 0、末尾玩法/实验成功标志齐全：`powershell -NoProfile -ExecutionPolicy Bypass -File tmp/combat-v0/check.ps1 -Name skills-v3`；日志 `tmp/combat-v0/skills-v3.out`。新增跨级等价、共同射速/换弹、机械上限和能力独立性验证；协议 round-trip 包含技能/max_hp。文档检查通过 |

验证注意：Windows GUI exe 的 PowerShell 表面返回可能留下旧 `$LASTEXITCODE`；本任务使用
`Start-Process -PassThru`、保留 Handle、`WaitForExit()` 和退出码/成功标志共同确认，辅助脚本位于临时证据目录。
首次技能回归检出旧换弹时长与玩家双倍射速断言，已改为当前契约并重新通过；不能把该次 wrapper 退出 0 当成功。

## 提交与剩余问题

- `64f7955`：统一角色等级技能与共享武器能力；原生 build、完整逻辑和文档检查通过。
- 02:46 系统时间复核：第二切片原生构建通过。首轮 Suite 63 组完成，镜像对射一致；
  旧导航回归用掉血判断到达，因新回避可吸收咬伤而失效，改为检查真实受击（HP或回避消耗）。
  首轮手枪实验误选主槽、固定射手水平射线命中头部，已定位并修正fixture，重跑前不引用其穿越/手枪数据。
- 03:04：公开资产原生离屏验收完成，23帧近/中/远、正侧后、俯视及走跑持枪连续帧；
  共享身体不可变、三实例姿态隔离通过。原八人友军/27件附件验收通过。证据 `tmp/combat-v0/assets/`，
  主执行者也实际查看 front/motion 组图；旧身体膝部接缝留作核心之后的独立改善。
- 03:15：回避31项和几何命中回归通过。双Smoker用例发现 `toy_game_update_actor_motion()` 清空仍在牵引中的
  control_disabled，已用最小条件修复并经真实双连接更新检查。网络新增测试的 map fixture 未绑定导致空指针，
  已修测试初始化；独立 protocol/pipeline/effects probe 通过。调试程序只在 tmp，没有改生产异常处理。
- 03:18：单枪真实射线对照完成，三个种子1337/9256/17175一致；3.9m、普通枪手AK、120HP固定胸部目标，
  每16ms执行正式枪械/回避更新，所有弹命中。首次命中400ms；无/65/90储备首次掉血400/1424/1936ms，
  缓冲0/1.024/1.536s，击败2192/3472/3728ms。证据 `tmp/combat-v0/buffer_probe.c`、`buffer.csv`。
  这是单枪定点测量，不代表多人集火下的保证。
- 首次 native Scale 失败于死亡感染者表现：实验为保留对象槽把 dying_ms 置为60000，超出既有动画合法区间；
  已改为正常播放后保持17ms尾相位，不改GPU管线。诊断证据 `tmp/combat-v0/scale-v5/`，待原生重验。
- 03:24：`powershell -NoProfile -ExecutionPolicy Bypass -File tmp/combat-v0/check.ps1 -Name combat-integration-v8`
  原生构建及完整逻辑通过，真实进程EXIT0且最终玩法、实验成功标记齐全；新AI16项、回避31项、
  三维命中/聚合/客户端延后结算、网络与owned清理均通过。日志 `tmp/combat-v0/combat-integration-v8.out`。

## 核心实验结果

Suite 使用正式 Game 路径、16ms步长、种子1337/9256/17175；
`tmp/combat-v0/suite-v8/` 与 `suite-v8-repeat/` 各63行，所有非CPU计时列一致。
命令为 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/combat_lab.ps1 -Stage Suite -OutputDirectory <新目录>`。
全表、构建时间、二进制/地图hash及dirty状态保存在CSV和manifest；未提交构建不能仅用HEAD代表全部源码。

| 条件 | 实测结果 |
| --- | --- |
| 8m、240HP固定靶，普通能力、已稳定瞄准 | AK 3.824s / 62.76真实HP每秒；SMG 13.456–14.096s / 17.03–17.84；AWP 5.440s / 44.12；霰弹7.216s / 33.26；手枪14.880s / 16.13 |
| 命中/射出弹丸，三个种子合计 | AK45/45、SMG105/221、AWP12/12、霰弹115/210、手枪120/120；霰弹命中率以弹丸为分母 |
| 普通/精英/机动模板阵营镜像 | 三模板×三种子，交换阵营后命中、掉血、击败时间完全一致，胜方阵营随镜像翻转 |
| 两名AK射手，约8.2m暴露穿越，120HP且其他能力相同 | 无回避3/3失败（1.232s倒地）；65和90回避均3/3成功（1.776s），分别剩17/42HP，储备均耗尽 |
| 单只普通感染者，先站立10s再脱离 | 无回避首伤64–80ms；65储备首伤8.128–8.144s；两者均能脱离 |
| 四只普通感染者，相同停留/脱离条件 | 65储备2.080–2.096s耗尽，2.080–2.144s首伤，最终剩61–63HP；无回避剩46–48HP，不存在站桩永久无伤 |
| 混战3友方/3枪手/4感染者 | 三种子均完成，3.984–5.520s；感染者能攻击两个人形阵营，无友军枪弹扣血 |

早期17.4m长暴露路线三档均被两名AK击败，保留诊断而不隐去失败；默认改为验证一次短转移。
当前结果证明指定条件下的行动缓冲，不保证穿越任意长度或火力。固定靶DPS包含到死亡为止的换弹、
散布和实际过量伤害扣除，不含初始瞄准；不能等同机械上限或无限时长循环。

03:28:44–03:32:37 的 Windows native 规模实验：
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/combat_lab.ps1 -Stage Scale -Repeats 3 -OutputDirectory tmp/combat-v0/scale-v8`。
同机NVIDIA GeForce RTX3050 Laptop、同场地/设置，4友方+16感染者与额外4枪手成对运行，每轮30s玩法时间、1875步，真实进程均exit0。

| 指标 | 4友方+16感染者（三轮） | 再加4枪手（三轮） |
| --- | --- | --- |
| mean frame，ms | 9.559 / 10.104 / 10.228 | 10.210 / 10.070 / 11.159 |
| P95 frame，ms | 11.539 / 12.750 / 13.146 | 13.019 / 12.767 / 14.711 |
| AI，µs/逻辑步 | 7.43 / 7.64 / 7.36 | 15.92 / 17.50 / 15.31 |
| 全部logic，µs/逻辑步 | 67.78 / 71.55 / 71.17 | 71.09 / 75.72 / 70.19 |
| 导航查询次数 | 0 / 0 / 0 | 0 / 0 / 11 |

帧采样排除首帧资源冷启动，包含交战死亡后的剩余窗口；不是全程恒定存活数量压测。
现有actor诊断日志开销在两组均保留。未发现新固定大开销、查询爆发或卡死；单次差异不能解释为精确渲染增量。

03:35:39–03:35:48 原生键盘复测exit0，观察到FPS Enter实际射击（shots0→1）、W移动、M切换RTS、
T结束/恢复前哨站、Enter同配置重置和第二次结束；新轮shots仍为0，等待确认键松手的修复通过。
证据 `tmp/combat-v0/interaction-v9-keypass/`。
当前沙箱无法取得窗口前台，SDL鼠标消息没有生效，鼠标选择LAB旗帜/右键部署尚未人工签收；
对应session命令链有逻辑覆盖。PrintWindow截图全黑，不能用作GPU画面证据；公开模型离屏证据另见上文。
另用既有GPU readback捕获45帧混战真实画面，exit0；`tmp/combat-v0/native-mixed-v9/mixed.png`，
已实际查看红色普通/精英枪手、友军、感染者及HUD；此诊断读回不混入性能数据。

03:38 同设置旧64感染者FULL场景复测exit0：`tools/gpu_performance_lab.ps1 -OutputDirectory tmp/combat-v0/core-native-64 -Rounds 1 -Scenes 2 -Stage Full`。
532帧、存活数全程64；mean15115µs / median14718µs / P9517579µs / P9923184µs，
enemy5976µs、geometry4479µs、upload1285µs、world495µs、submit5416µs、GPU3.691ms。
与开始时mean15837µs/P9519134µs相比没有明显新增固定开销；P99由20855µs升至23184µs，
单轮自然波动不能证明优化或长期稳定帧率。两次配置JSON一致、均使用NVIDIA物理GPU。

03:47 审查收口：恢复死亡体继续落地、普通冲击落地释放控制，Smoker活跃控制保持；
session/net 普通和付费复活复用统一入口清理旧运动，并按max_hp封顶。
专项AI/回避/net/effects全部通过。实验LAB旗帜补标准队形，真实RTS部署链回归检查四名友军目标互异、
敌军不受指挥、下令不直接移动当前位置；这项逻辑验收不冒充物理鼠标验收。

03:48 最终核心验证：`powershell -NoProfile -ExecutionPolicy Bypass -File tmp/combat-v0/check.ps1 -Name combat-core-v10`
原生构建、完整逻辑真实exit0，日志 `tmp/combat-v0/combat-core-v10.out`。
重跑Suite至 `tmp/combat-v0/suite-core-v10/`，63行与v8仅build与CPU计时列不同，所有玩法结果一致。
核心通过，下一步为独立的回避动作和共享身体质量附加阶段。
