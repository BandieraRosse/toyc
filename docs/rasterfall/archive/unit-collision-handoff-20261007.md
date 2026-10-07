# 常态单位体积碰撞交接（2026-10-07）

> 状态：历史交接现场，已由后续收尾记录替代
> 所有者：Rasterfall Game
> 日期：2026-10-07

以下保留交接时的事实；后续修正和最终验证见[单位碰撞收尾](unit-collision-completion-20261007.md)。
交接会话最后按用户要求停止开发和运行验证；没有提交。
当前契约入口为[玩法架构](../architecture/gameplay.md#常态单位体积碰撞)，
优先级仍归[唯一活动计划](../plans/tactical-foundation.md)。本文件只记录交接现场。

## 用户范围与限制

- 增加常态单位体积碰撞；不处理联机。
- 曾因另一会话性能测试暂停构建与测试，后来明确恢复并要求快速完成。
- 随后追加关闭玩家受击反冲：目前按玩家被咬后自动击退感染者的旧被动反应处理，已删除。
- 最后要求停止，不再继续实现、构建或测试；后续由新会话接手。

## 工作区实现

新增 `rasterfall/lib/game_unit_collision.inc`，由 `game.c` 在身体半径定义后包含。
使用现有 RFU 半径的竖直圆柱，水平扫掠、固定点接触裁剪与切线滑行，按身体高度区间判定。
扫描有界 actor/enemy 池，用扫掠 AABB 排除远处单位，无候选截断、位置缓存、分配或 RNG。
正常玩家、AI 和感染者互相阻挡；死亡、倒地、base_core、动画展示不作为普通实体。
已经重叠的身体允许向外或不加深重叠的方向脱离，没有自动解开任意密集出生的算法。
特殊控制、击退、感染者击飞和 Charger 冲锋保留特殊运动策略。

`game.c` 原移动检查拆成 terrain helper，真实池成员通过动态检查，脱离池的导航探测仍只看地形。
玩家地面移动分段滑行，空中正常水平运动检查身体和高度；身体不作为地面支撑。
普通感染者追逐改为动态滑行，软分离最终位移也不能绕过身体检查。
咬人距离覆盖双方半径加接触余量，防止实体接触却无法近战。

拥堵处理还包含：

- actor 路线查询失败时等待，碰撞后失效直达证明但保留静态路线；完全受阻尝试局部侧移。
- enemy 新字段 `nav_unit_blocked` 区分单位阻挡和地形停滞，避免排队触发地形修复。
- 旧集团路线可绕过被身体占据的近路点，但后续接入经过原静态检查。
- `nav_next_waypoint()` 启用现有端点接入，处理滑行后离开可走格中心的身体。
- 普通感染者地形移动与旧软分离加 2 RFU 接触余量，避免贴在静态线段证明的闭边界。
- 感染者完全受阻时尝试切向侧移，失败后尝试反向；当前覆盖默认流场和旧集团。

最后追加的玩家反冲改动：删除 `bite_player()` 的 `knockback_ready` 分支以及只被该分支使用的
`push_enemy_from_player()`。仍扣血、更新咬人冷却、受伤反馈和 BITE 事件。
没有关闭显式推击、爆炸击飞、Charger 命中或其他特殊技能。
此最后追加行为尚未补充专项断言或同步相关旧文档，需要新会话核对。

策略规则/配置 `policy_version` 升为 3，API 和算法版本不变。
更新 `rf_tactical.h`、未训练内置配置、`tools/tactical_train.py`；旧历史训练配置没有覆盖。
根 Makefile 和 Windows Makefile 增加 include 依赖。架构、任务路由和活动计划已有更新。

## 验证事实及未通过项

恢复测试后已完成多轮 Windows native 编译，没有当前已知编译错误。
最新完整逻辑回归退出 **211**，日志 `tmp/unit-collision-final-test.log`：

```text
UNIT-COLLISION blocking, slide, sweep, overlap escape, height, static planning and contact melee: PASS
ramp player blocked x=2844 next=2920 ground=600
primitive gameplay failure: 17
```

定位 `rasterfall/src/rasterfall_logic_test.inc` 的 `primitive_gameplay_logic_test()`：
先让一个感染者走上坡接近 x=3300 的玩家，随后把玩家移回 x=-500，要求每步直接移动 76 RFU 穿过
同一坡道。感染者仍活着且留在坡顶。删除被动反冲后它不再飞离，玩家被该身体合法阻挡，触发断言17。
下一步应隔离第二阶段的地形检查与遗留活体（例如清除该阶段不需要的感染者，或使用脱离池的探测体），
保留坡道地形契约，同时不要取消正式单位碰撞以让旧测试通过。修正后继续完整回归；后面可能还有
假设可穿过单位或依赖被动反冲的旧用例。

独立战术验证曾通过 `TACTICAL GAME: 601 checks, 0 failures`，日志
`tmp/unit-collision-tactical-test.log`。该日志早于最后删除玩家反冲和调整默认流场出生布局，
必须在这些最后改动后再跑，不能声称最终版本已通过601项。
文档检查曾通过340文件，`git diff --check`曾通过；交接文档和最后修改后仍应重新检查。
没有物理 GPU 手感验收或本功能性能签收。

导航调试前序事实：

- 旧墙角全员通行和64单位西走廊曾在前序版本通过，继续到默认流场回归276。
- 默认流场原64单位用例仅2个到达，其出生间距180/220小于双方身体半径和260，初始有大量交叠。
  最新将该用例出生网格改为300/300，仍要求全部到达、伤害发生、单共享场构建和逐tick预算。
  **最新完整回归在更早的211退出，因此这个出生布局改动尚未执行到。**
- 旧西走廊同样改为同一地板上的清晰8×8出生网格；允许900tick通过，保留所有64成员出口断言。
  旧墙角允许1200tick排队。两例临时禁止咬人反冲的 cooldown 设置仍在，现在删除被动反冲后可核对是否保留。
- 旧西走廊预算原 `nav_searches <= 10` 改为共享寻路 `<=10`、整段局部接入 `<=64*3`。
  前序实测共享6、局部150、修复0，额外局部接入是动态滑行后重新接入静态路线。
  该变更应明确审查其成本，不能将放宽后的通过当成性能优化；默认流场仍要求无旧BFS。
- `combat_ai_test.inc` 五个端点从无法容纳实体的50RFU密集排列改为400RFU间距，并扩大障碍边界；
  独立 AI 回归曾通过。需要保留原路线、闭门等待、重开和静态预算断言。

## 继续执行入口

1. 先读 `docs/rasterfall/README.md`、玩法架构和活动计划，再检查未提交 diff。
2. 修正上述坡道测试阶段隔离问题，增加玩家被咬后无被动击退的稳定契约断言，并核对旧文档。
3. 完整 Windows native `test`；根据实际新失败逐一判断旧 fixture 与真实行为回归，不任意放宽断言。
4. `tactical-test` 验证最终规则/隔离预测一致性；文档检查与 `git diff --check`。
5. 如继续做实机手感/密集成本验证，先确认另一会话的性能工作状态，独立报告。

正常命令是 `powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test`，
以及同脚本 `tactical-test`。`test` 默认通过 `rasterfall-rebuild` 强制全量重编。
本会话在完成多轮全量原生构建后，为缩短逻辑迭代，在独立子进程设置
`MAKEFLAGS=-o rasterfall-rebuild`；仍保留实际头文件、`.d` 和显式依赖，只跳过该伪目标。
不修改仓库构建脚本来绕过检查；最终若担心依赖完整性，可运行标准全量入口。

## 共享工作区保护

本会话新增碰撞与碰撞用例两个 `.inc`，其余修改见 diff；不要提交 `tmp/` 或 `build-windows/`。
以下是另一会话性能工作的修改，不能清除、覆盖或归入本任务：

- `rasterfall/src/rf_scene_performance.inc`
- `docs/rasterfall/guides/rendering-performance.md`
- `tools/rf_cpu_profile.py`、`tools/rf_cpu_profile_report.py`
- `docs/rasterfall/archive/cpu-profile-20261007.md`、`cpu-profile-detail-20261007.md`

主逻辑测试文件原磁盘以CRLF为主，小补丁会形成混合换行；本会话曾仅对该文件恢复CRLF，
最后几次补丁后应再核对。保留UTF-8/BOM状态，禁止全仓格式化或覆盖其他会话修改。
本会话没有仍运行的自有构建/测试任务；最近测试会话40349已退出211。
