# 战斗 V0 实验场

> 状态：当前
> 所有者：Game Runtime 实验编排；Game 战斗真值
> 事实入口：`rasterfall/src/rf_combat_lab.inc`、`tools/combat_lab.ps1`

## 现场操作

前哨站南排最西侧 `LOW PRESSURE` 空性能场同时供战斗实验使用，两种实验不能同时启动。
场地北侧横路的南边缘（LOW PRESSURE 一侧）设有两个物理终端，总投影标注 `COMBAT LAB / LOW PRESSURE`。
终端与投影朝北，玩家从道路面向南方操作时，背景为 LOW PRESSURE 场地，避开北侧往返展示区。
西侧终端投影为 `E SELECT COMBAT TEST`，打开与 `--combat-lab` 共用预设的战斗选择界面；
东侧终端投影为 `E RESULTS`，显示最近一轮状态，无结果时标注 `NO SAMPLE`。
FPS 靠近按 E，方向键选择预设，F2 切换玩家参与或脚本观察，
Enter 开始。实验最多运行 30 秒玩法时间；T 中止并查看结果，Esc 暂停。
完成后返回开始位置，结果页 Enter 用同一种子和配置重置，F2 返回配置页。

玩家参与模式使用正常枪械、移动、换弹和 M 切换 RTS。左键点选/框选普通友军并右键部署，支持数字编组；
实验临时 `LAB` 旗帜仅保留场地标记，不参与队友寻路或部署。
玩家本人也沿用 RTS 选择与移动。穿越实验向场地东端移动；近战实验先保持贴身 10 秒再向东脱离，
玩家可自行提前逃离，但结果明确标记为 `player`，不与脚本数据混合。
观察模式把玩家移到场外并排除其战斗索敌，实验单位在同一个真实世界运行。

预设索引以程序 `--help` 和终端为准，当前分组如下：

| 分组 | 内容 | 固定条件 |
| --- | --- | --- |
| 武器 0–4 | AK、SMG、AWP、Shotgun、Pistol | 8 米、240 HP 静止普通感染者；预先稳定瞄准，正式射线/弹丸/换弹 |
| 对射 5–10 | Standard、Elite、Mobile 各一对及阵营镜像 | 同模板、同 AK，出生点固定；镜像交换东西两侧阵营，朝向均指向对手 |
| 穿越 11–13 | 无/中/高回避 | 两名定点 AK 射手、同一条约 8.2 米暴露路径；射手按能力瞄准准备后射击 |
| 近战 14–17 | 单只/四只普通感染者 × 无/中回避 | 同生命和其他能力；脚本 10 秒后尝试向东脱离 |
| 混战 18 | 3 名友方、3 名枪手、4 只感染者 | 感染者攻击两方；普通与精英混编 |
| 规模 19–20 | 4 名友方 + 16 感染者，对照额外 4 枪手 | 同场地、同 30 秒 gameplay 采样窗；记录 native 帧间隔和 AI/世界工作量 |

当前武器目录没有机枪，实验不虚构机枪配置。穿越和近战用能力等价构造隔离回避：
非回避能力全部是最终档位 3，即 120 HP、100% 移动能力、相同武器与精准。
无/中两档用 Lv1、其他技能精通，回避分别普通/大师；高档用 Lv2、其他技能普通、回避精通。
最终回避档位分别 1/4/5，容量 0/65/90。等级不另加倍率；日志中的回避使用千分之一单位。

## 可复现采样

先用 Windows 原生入口构建并暂存最新 exe 与资源；脚本不会暗中重新编译。
每次使用新的输出目录，保留 exe/map 哈希、HEAD、dirty 状态、真实起止时间、stdout/stderr 与运行日志。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/combat_lab.ps1 -Stage Suite -OutputDirectory tmp/combat-v0-suite
powershell -NoProfile -ExecutionPolicy Bypass -File tools/combat_lab.ps1 -Stage Scale -Repeats 3 -OutputDirectory tmp/combat-v0-scale
powershell -NoProfile -ExecutionPolicy Bypass -File tools/combat_lab.ps1 -Stage Play -Preset 18 -OutputDirectory tmp/combat-v0-play
```

Suite 默认每配置三个固定种子 `1337`、`9256`、`17175`，通过与现场相同的开始/前置命令/固定步/采样/清理函数推进，
不启动渲染。可用 `-Preset` 定向验证一个配置。Scale 是实际 Windows native GPU 场景，
使用正常墙钟与呈现策略、正常固定逻辑步，在同机器同设置按基线/加枪手成对采样。
本机 Scale 固定选择 NVIDIA（`RF_GPU_VULKAN_VENDOR_ID=0x10de`），脚本结束恢复此前环境变量。
帧均值、中位数、P95、最大值在 CSV，AI、enemy、logic 是累计 CPU 微秒，可除以 ticks 得到每逻辑步成本。
原生帧间隔从第三次 begin-frame 开始记录，排除首帧资源和管线冷启动区间；逻辑采样仍覆盖整个实验。
它记录的是交战从开始到结束的实际工作量，包含死亡后的剩余采样时间，不能冒充全程存活数量恒定的压力测试。
Suite 的 `frames=0` 是未渲染的明确标记，不能据此给 GPU 性能结论。

直接启动当前 package 也可使用 `--combat-lab`、`--combat-lab-observe`、`--combat-lab-suite`、
`--combat-lab-output`。编译时尝试记录源码 Git 描述并记录编译时间；若构建环境固定 PATH 未包含 Git 等原因导致描述为空，
直接运行会明确写 `source-build-unrecorded`，不能把它当已确认的提交标识。
脚本另将启动时 HEAD 写入 `RF_COMBAT_COMMIT`，manifest 的二进制哈希与 dirty 状态用于区分未提交构建，验收应保留这组证据。
`--combat-lab-auto-exit` 在单次 native 实验完成后退出，
不以 PowerShell 表面返回代替进程退出码。

## 结果语义和所有权

CSV 每行包含配置、seed、16 ms 固定步、repeat、`player/scripted`、结束状态、实际生命伤害、
shot/pellet/hit、换弹次数、回避消耗/耗尽、双方倒地和感染者死亡、首伤与首耗尽时间、穿越结果。
`health_damage` 是参与 actor 对外造成的实际生命损失，`damage_taken` 是参与 actor 的实际受伤；
普通感染者没有 actor 射击计数，近战主要观察后者。
首伤、首耗尽和剩余生命/回避指主试验对象；固定靶武器实验中的主对象是射手，因此没有受到反击时首伤为 `-1`。
`ttk_ms=-1` 表示未击杀或不适用；超时从不写伪造 TTK。人形对射以第一名倒地视为击败，
而非等待后续复活/尸体生命周期。穿越/近战用 elapsed_ms 与 crossed 表示生存和脱离。
summary.json 汇总三种子的范围/中位数；TTK 中位数只含确实完成击败的轮次，超时另计。
有效 DPS 使用真实 `health_damage / (elapsed_ms / 1000)`；武器测试是从已稳定瞄准的首发所在固定步到 240 HP 目标死亡，
包含射击间隔和换弹，不包含初始索敌与瞄准准备，不等同无限时长持续 DPS。
对射与穿越则包含 AI/射手开始瞄准的时间。summary 同时汇总 shot、pellet、hit、换弹和 hit/pellet 比例；
霰弹枪一次 shot 含多枚 pellet，因此 hit/shot 可以大于 1，不能当命中率。

Runtime 仅保存预设和实验对象身份、实验前玩家、临时旗帜、只读统计及样本；伤害、回避、
装备、弹药、死亡仍由正式 Game API 执行。开始前拒绝占用中的场地；感染者槽在实验内保留到清理，
actor 按 actor_id 与 combat_generation 共同匹配，槽位被新角色复用后不删除新角色。
清理不调用 killall，不清空全世界；只清本轮对象及其投掷物，
恢复玩家/镜头/随机流并移除临时旗帜。地图切换前中止实验；普通展示请求不被实验重置。
玩法已移除金钱和击杀奖励；实验清理只撤销自身拥有且身份代际仍匹配的对象，不修改场外对象。
日志先在内存累计，单轮现场结束或整批 Suite 结束才写盘，不在每次射击时同步刷盘。

特感控制兼容由[玩法架构](../architecture/gameplay.md)和核心逻辑回归保护，实验场不重新实现 Smoker、Charger 或 Tank 状态机。
