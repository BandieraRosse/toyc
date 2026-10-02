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

1. **完成切片**：能力映射与代表模板；统一机械射速、换弹、散布、移动、耐力生命；兼容原等级升级。回避/弱点/承载映射尚待消费，未冒充实际效果。
2. **进行中**：阵营、共享射线/结算和普通/精英枪手；队友命令与敌方决策分离，低频感知。
3. 待办：连续回避、攻击分类/聚合、恢复与事件；平衡与必要快照/复位。
4. 待办：共享组件模型、固定战斗实验/现场入口/日志、FPS/RTS 可玩闭环。
5. 待办：原生资源/视觉/规模验证、核心收口、稳定架构与操作文档。
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

尚无实现提交。尚未验证新战斗、模型、网络、回避与实验功能；不能把基线通过当作新增功能验收。
