# 靶场、战术对抗、训练与回放

> 状态：当前
> 所有者：Rasterfall 战术工具
> 事实入口：`build-windows/rf-tactical.exe --help`、`tools/tactical_train.py --help`

从仓库根目录运行。实验完全使用 Windows 原生 C 仿真，不初始化 SDL/GPU，也不等待
实时帧间隔。规则和正式 Game 的接入边界见[战术 AI 架构](../architecture/tactical-ai.md)，
参数的现实依据与设计取舍见[枪械基准](../reference/tactical-weapons.md)。

## 构建和靶场

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 tactical-build
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 tactical-test
.\build-windows\rf-tactical.exe range --samples 2000 --weapon both
```

靶场输出 JSON，分别测试全身、半身、探头、横移目标，覆盖单发、三发点射、连续射击。
距离从 5m 到 100m；输出实测与同一模型的期望命中率、单发伤害期望、含换弹 DPS、
散布与基准战士 TTK。未在测试窗口内击杀的样本单独报告 censored，不能用成功击杀
的均值假装所有样本的 TTK。精确参数以当前 `--help` 和输出为准。

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

内置名称 `simple`、`mechanical`、`utility` 对应策略一至三。也可用
`rasterfall/config/ai/` 下的 `.cfg` 选择内置 solver 与数值权重；没有动态算法代码加载。
`--budget` 对双方设置相同的逻辑评价预算。预算耗尽时剩余成员保持合法 HOLD。
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
评分及最终计划。日志 tick 是 20ms 物理步，200ms 的决策轮次每十步发生一次。
原生 inspect 读取指定步的记录；Python inspect 同时找出该步最近的决策。
HTML 可离线打开，滑动时间、选择队伍/成员，查看位置、射击、健康、候选与评分。
回放只消费权威日志，不重算对局，也不改变训练结果。

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
不是神经网络或已完成的 Beam Search。扩大训练时改变输出目录，保留旧证据。

仓库已提供一次真实训练的
[`trained-v1.cfg`](../../../rasterfall/config/ai/trained-v1.cfg)，可直接作为 `--a` 或 `--b`。
训练和最终构建的复核结果见[初版验收记录](../archive/tactical-foundation-20261007.md)。

```powershell
.\build-windows\rf-tactical.exe match --a rasterfall/config/ai/trained-v1.cfg --b mechanical --map-seed 10005 --squad 5 --weapon rifle --log tmp/tactical-match.jsonl
python tools/tactical_report.py tmp/tactical-match.jsonl --output tmp/tactical-match.html
```

`tools/tactical_lab.ps1` 提供相同操作的 wrapper；原生 `--help` 和两个 Python 工具
的 `--help` 是参数事实入口。完整玩家构建和 package 同时包含 `rf-tactical.exe`
及策略配置；Toyc self 构建不包含这个 hosted 诊断程序。
