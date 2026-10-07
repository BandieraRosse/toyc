# 靶场、战术对抗、训练与回放

> 状态：当前
> 所有者：Rasterfall 战术工具
> 事实入口：`build-windows/rf-tactical.exe --help`、`tools/tactical_train.py --help`、`tools/tactical_benchmark.py --help`

无图形命令从仓库根目录运行，使用 Windows 原生 C 仿真，不初始化 SDL/GPU，也不等待
实时帧间隔。规则和正式 Game 的接入边界见[战术 AI 架构](../architecture/tactical-ai.md)，
参数的现实依据与设计取舍见[枪械基准](../reference/tactical-weapons.md)。

当前规则和策略版本为 2，权威为正式 `toy_game`。旧 `trained-v1.cfg` 不再出现在
终端中，也不能作为新版本策略加载；已有版本 1 报告只能作为旧模型历史结果。
新训练由 `tools/tactical_train.py` 生成版本 2 参数。游戏内是已加载地图的真实碰撞；
CLI 的种子地图是生成场景，跨前端复现须同时匹配几何、规则和初始状态。

## 新机械算法与攻防对打

当前游戏终端和 CLI 默认双方均为“机械 M0（新框架）”，分别执行进攻和防守。
CLI 名称是 `mechanical-v3`；旧 `mechanical` 保留作旧接口对照。框架边界和算法扩展方式见
[算法框架](../architecture/tactical-ai.md#策略算法框架-api-1)。M0 不是已训练的强策略。

```powershell
.\build-windows\rf-tactical.exe algorithms
.\build-windows\rf-tactical.exe match --a mechanical-v3 --b mechanical-v3 --log tmp/m0-match.jsonl
.\build-windows\rf-tactical.exe match --a rasterfall/config/ai/mechanical-v3.cfg --b mechanical-v3 --order-a defend --order-b attack
.\build-windows\rf-tactical.exe batch --a mechanical-v3 --b mechanical-v3 --pairs 2 --squad 6 --weapon both
python tools/tactical_benchmark.py --policies mechanical-v3 --opponents mechanical-v3 --pairs 1 --shot-seeds 1337 --squads 4 5 6 --output tmp/m0-benchmark
```

`batch` 交换攻防，并可覆盖双方枪种。新算法快照不预计算旧候选评分，回放中候选/关系为空、score 为 null，
不是零火力或零价值。结果中的 preparation_elapsed_seconds 是包装/准备耗时，solver_elapsed_seconds
包含按需查询；query_seconds 是其中的查询耗时。逻辑工作单位不等于毫秒，不能跨算法直接当成 CPU 成本。
配置中的参数 schema 和 algorithm_version 由具体算法声明。现有 tactical_train.py 仍只训练旧 utility，
不可用它声称训练了 M0；新算法训练器和强度分层将在后续阶段接入。

## 构建和靶场

### 游戏内入口与终端

Windows 完整构建并暂存后，在前哨站中央指挥桌选择“AI 策略对抗场”或“武器靶场”，
点击地图字段展开下拉列表，选定目的地后点击右侧预览画面框才开始加载；出生点首帧完成后冻结画面，
再点击确认部署。进入地图保留普通 HUD、移动、跳跃和 M 切换视角。
后台加载期间显示阶段文字和估算进度条，光标与界面输入由主线程持续处理；完成前不能部署。
切换目的地取消旧预览，新目的地仍需点击画面框；关闭时等待后台任务安全退出后返回大厅。
出生点附近控制终端按 E 打开，点击参数字段展开列表，悬停高亮，
当前值显示选中标记；点击空白收起列表，Esc 先收起列表、再退出终端。
种子栏点击后直接输入十进制整数，首次输入替换原值，Backspace 删除，Enter 完成，Esc 撤销本次编辑。
对抗场与靶场均接受 0–4294967295（32 位无符号整数，最多 10 位数字）；空值和越界值不能开始新局或重置。
开始新局应用当前配置并运行；重置应用配置并停在初始状态；暂停/继续不重新建局。
待应用参数与当前对局结果分别展示。菜单提示音区分悬停、点击、值变更、确认和退出。

对抗场支持两方内置策略、4–6 人、步枪/SMG、攻守交换、射击种子与工作预算。
点击观战进入 RTS，WASD 平移、滚轮缩放，右侧单位状态列表显示生命、回避和阵亡状态。
E 重新打开控制台；友方正式 actor 可接受普通 RTS 移动/停止命令，该命令优先于策略移动。地形固定为种子 100。
Esc 暂停菜单可沿正常世界切换返回前哨站。

靶场选择武器、AI 射击模式、距离、全身/半身/探头假人和随机种子。开始后 AI 自动打靶，
玩家试射按钮将玩家放到所选固定射击位；鼠标瞄准、左键射击、R 换弹，E 返回终端。
各距离的实体标靶同时存在，玩家可自由移动、跳跃和瞄准其他标靶，按实际射距结算；
AI 继续使用终端配置的固定射道。AI 与玩家使用独立的真实标靶，弹着及统计来自正式射击事件；跨射手打同一标靶的混合试次不计独立 TTK。
射道支持无限备弹；假人击杀后重建生命和回避用于下一试次。完成 TTK 不包含尚未击杀的试次。

导出结果按钮保存 `tactical-result-*.json` 与同名 `.csv` 到实际运行目录
`build-windows/rasterfall-windows/`。重置前和对抗结束也导出；终端显示导出文件名或失败。
JSON 保存规则版本、种子及对抗策略参数，CSV 适合比较每名战士或各射手/射道的计数；
玩家移动试射时可用 actual_distance_mean/min/max 核对实际射距。
批量胜率、训练和完整逐步决策分析仍使用下面的无图形入口。

原生 UI 验证：先 `NativeCodex.ps1 test` 暂存资源，再运行
`python tools/tactical_ui_check.py --kind arena --output tmp/tactical-ui-arena`，
靶场将 kind 改为 `range` 并使用另一输出目录。脚本只操作自己创建且取得前台的窗口，
保存下拉悬停、运行、观战/试射及重置截图、只读状态审计和真实退出码。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 tactical-build
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 tactical-test
.\build-windows\rf-tactical.exe range --samples 2000 --weapon both
```

靶场输出 JSON，使用正式 Game actor 测试全身、半身、探头、横移目标，覆盖单发、三发点射、连续射击。
距离从 5m 到 100m；输出实际命中率、每发生命损失、实测 DPS 和完成试次 TTK。
截止时未完成的试次单独报告 censored 和时长；成功击杀均值不代表所有试次。
半身/探头由真实挡板遮挡，移动目标走正式 actor 导航。精确参数以 `--help` 为准。

保存 JSON 时避免 PowerShell 默认重定向编码，可用 Python 原样捕获标准输出：

```powershell
python -c "import pathlib,subprocess; pathlib.Path('tmp').mkdir(exist_ok=True); pathlib.Path('tmp/tactical-range.json').write_bytes(subprocess.check_output(['build-windows/rf-tactical.exe','range','--samples','2000','--weapon','both']))"
python tools/tactical_range_report.py tmp/tactical-range.json --output tmp/tactical-range.html --csv tmp/tactical-range.csv
```

## 单局和批量比较

```powershell
.\build-windows\rf-tactical.exe match --a simple --b mechanical --map-seed 100 --shot-seed 1337 --squad 4 --weapon rifle --log tmp/tactical-match.jsonl
.\build-windows\rf-tactical.exe batch --a utility --b mechanical --map-seed 10000 --shot-seed 1337 --pairs 8 --squad 6 --weapon both
```

内置名称 `simple`、`mechanical`、`utility` 对应策略一至三，`beam` 是联合动作搜索。也可用
`rasterfall/config/ai/` 下的 `.cfg` 选择内置 solver 与数值权重；没有动态算法代码加载。
`--budget` 对双方设置相同的逻辑工作预算。Beam 同时计费前缀评分、预测推进和叶评分，
基线只计费候选/目标评价；相同数值不代表相同 CPU 时间。预算耗尽时返回合法计划。
主要人数为四至六人，双方共享战士和武器，差别来自策略、攻守岗位和地图。

单局默认 A 进攻、B 防守；`--order-a attack|defend` 和 `--order-b attack|defend`
可改变命令。进攻目标是地图据点范围，清空范围并完成占领后转防守。
批量在同一地图/射击种子下交换 A/B 的攻守岗位，`both` 使用两种枪的镜像对抗。
据点有真实防守优势；不要拿单一岗位胜率比较策略强弱。

`--map-seed` 控制地形，`--shot-seed` 控制战斗随机采样。相同二进制、规则、策略参数、
种子、人数、预算、命令和时限应得到相同最终 hash。跨编译器或架构的浮点位级一致性
尚未作为保证；日志提供版本与参数，规则变化后旧结果不可直接续作同一基准。

## 查看对局和决策

```powershell
.\build-windows\rf-tactical.exe inspect --log tmp/tactical-match.jsonl --trace-tick 100
python tools/tactical_report.py tmp/tactical-match.jsonl --tick 100
python tools/tactical_report.py tmp/tactical-match.jsonl --output tmp/tactical-match.html
```

JSONL 保存初始配置、地图、策略参数/hash、每个物理步状态和每轮求解的候选事实、
评分及最终计划。日志 tick 是 16ms Game 规则步，决策在跨过 200ms 边界的下一步发生。
原生 inspect 读取指定步的记录；Python inspect 同时找出该步最近的决策。
HTML 可离线打开，滑动时间、选择队伍/成员，查看位置、射击、健康、候选与评分。
回放只消费权威日志，不重算对局，也不改变训练结果。

## Beam 搜索与独立基准

```powershell
.\build-windows\rf-tactical.exe match --a beam --b mechanical --map-seed 2000 --squad 6 --budget 128 --log tmp/beam-match.jsonl
python tools/tactical_report.py tmp/beam-match.jsonl --output tmp/beam-match.html
python tools/tactical_benchmark.py --policies beam rasterfall/config/ai/strategy3.cfg --map-seed 30000 --pairs 8 --shot-seeds 1337 424242 98765 --squads 4 5 6 --jobs 3 --output tmp/beam-benchmark
```

默认宽度 2、最多 4 分支、800ms 预测已在 `beam-v1.cfg` 中保存。原生
`--beam-width`、`--beam-branches`、`--beam-horizon-ms` 可覆盖双方 Beam 配置，
不改变其他 solver。增加这些参数时同步检查预算和日志中的耗尽情况，较大时域
在小预算下可能只返回 HOLD。16ms 步增加了同一预测时域的步数；128 预算可能提前停止成员扩展，应以日志中的工作数和耗尽标记为准。

决策的 `beam` 记录每层扩展数、保留联合动作、父排名、预测结果、最终选择和
健康/进度/火力/风险/队形/终局分数分项。候选表的 Beam 分数是某次联合前缀评价，
不能当作单个位置的独立价值；未评分项为 null。HTML 同时展示联合计划与最终预测比较。
`uncertain_shots` 在版本 2 中保守标记采样预测的不确定性；预测深复制正式 Game，
使用与主世界隔离的随机流，不能把单次预测击杀视为确定胜利。旧均值结算已退役。


## 初步训练

```powershell
python tools/tactical_train.py --generations 3 --population 8 --elite 3 --output tmp/tactical-training
```

训练使用确定性 CEM 优化六个 utility 权重；全部对局由 native exe 完成，Python 只安排
实验和统计。训练池包括三个内置对手、步枪/SMG、交换攻守。默认保留地图种子与训练
地图互不重叠，评测四、五、六人小队。种子重叠会拒绝；已有非空输出目录不会覆盖。

生成 `trained-v1.cfg`、逐候选评价 `evaluations.jsonl` 和 `report.json`。
训练入口先保存 native exe 副本，所有批次使用同一副本；并行开发时重建工作区不会
混入另一套仿真规则。report 记录采样配置、exe SHA-256、训练与保留种子、训练轨迹、各对手结果和初始
utility 的同条件对照。保留集只用于最终评测，不用于选权重。这是参数训练，
不是神经网络或 Beam 参数训练。扩大训练时改变输出目录，保留旧证据。

仓库已提供一次真实训练的
[`trained-v1.cfg`](../../../rasterfall/config/ai/strategy3.cfg)，可直接作为 `--a` 或 `--b`。
训练和最终构建的复核结果见[初版验收记录](../archive/tactical-foundation-20261007.md)。

```powershell
.\build-windows\rf-tactical.exe match --a rasterfall/config/ai/strategy3.cfg --b mechanical --map-seed 10005 --squad 5 --weapon rifle --log tmp/tactical-match.jsonl
python tools/tactical_report.py tmp/tactical-match.jsonl --output tmp/tactical-match.html
```

`tools/tactical_lab.ps1` 提供靶场、对局、回放、训练及 benchmark 操作的 wrapper；原生 `--help` 和 Python 工具
的 `--help` 是参数事实入口。完整玩家构建和 package 同时包含 `rf-tactical.exe`
及策略配置；Toyc self 构建不包含这个 hosted 诊断程序。
