# 枪战 AI 主体框架初版验收

> 状态：历史；2026-10-07 主体框架初版与初步参数训练已完成
> 归档原因：记录 2026-10-07 的初版实现、一次参数训练与 Windows 原生验证
> 当前合同：[战术 AI 架构](../architecture/tactical-ai.md)
> 当前操作：[战术实验指南](../guides/tactical-lab.md)

## 交付范围

交付独立 `rf-tactical.exe`：共享标准 AI 战士和步枪/SMG 模型、靶场、种子据点地图、
20ms 权威战斗、200ms 只读战术快照、八个位置候选、双向关系矩阵、路径火力积分、
进攻/防守及占领后命令转换。求解器为简单、机械、utility 与其训练权重版本。
无图形批量对局、双方交换攻守、复制世界独立推进、逻辑预算、JSONL 日志、逐轮决策
查看和离线 HTML 回放均可运行。Windows 构建和 package 规则包含控制台程序及配置。

本次没有将新体系替换进正式 `toy_game` / RTS，也没有完成 Beam Search。
正式 actor / 地图的适配是后续明确的接入工作。动态算法文件、DSL/VM、热加载及语言
语义按用户指示延期；`.cfg` 只选择内置求解器和数值参数。
枪械现实来源、工作值及抽象边界归[武器基准](../reference/tactical-weapons.md)。

## 实际训练

正式命令如下；训练入口后续补了自动冻结 exe 的功能，此次执行期间由开发者冻结
工作区 exe 并另外保存其副本，整个实验使用同一个二进制。

```powershell
python tools/tactical_train.py --generations 3 --population 8 --elite 3 --train-pairs 3 --holdout-pairs 8 --budget 128 --duration-ms 60000 --output tmp/tactical-training-v2
```

使用确定性 CEM 采样种子 20261007，训练地图 100–102，保留地图 10000–10007，射击
基础种子 1337，每张图递增 7919。每个地图/对手组合分别进行步枪、SMG 两种镜像
对抗，并交换攻守；对手是 simple、mechanical、初始 utility。
训练人数为 4，保留评测人数为 4、5、6。
每轮人口 8、保留 3，共 24 个候选；总计 864 局选参、288 局训练策略评测及 288 局
初始 utility 同条件对照，合计 1,440 局。记录的总墙钟用时为 579.39 秒，包含 Python
编排及 native 进程开销，不能解释为图形引擎的帧率。

策略只按训练集选择，fitness 为胜率加 0.025 倍有界有效健康差。
最佳训练候选为第 3 代第 6 个，训练得分 23/36；保留集不反馈权重。
训练前一次试验因导航持续重规划的振荡而中止，其结果已废弃。
修复采用完整导航路线的前方候选与持续执行路段，随后重新开始本次正式训练。

实际交付参数为
[`trained-v1.cfg`](../../../rasterfall/config/ai/trained-v1.cfg)，策略内容 hash 为 `d08d1a61`
（预算 128）。以下为同条件保留集统计，每格均为 32 场、无平局：

| 人数 | 对手 | 训练策略胜局 | 初始 utility 胜局 |
| --- | --- | --- | --- |
| 4 | simple | 22 | 23 |
| 4 | mechanical | 14 | 15 |
| 4 | utility | 20 | 16 |
| 5 | simple | 23 | 24 |
| 5 | mechanical | 15 | 14 |
| 5 | utility | 18 | 16 |
| 6 | simple | 19 | 19 |
| 6 | mechanical | 16 | 15 |
| 6 | utility | 18 | 16 |
| 合计 | 三个对手 | 165/288 = 57.29% | 158/288 = 54.86% |

总体提升 7 胜局、2.43 个百分点，主要来自对初始 utility 的改善。
对 simple 有 2 胜局退步，对 mechanical 总计 45/96，仍未取得整体优势。
训练策略进攻胜局 39/144、防守胜局 126/144；据点防守优势明显。
因此不能将总体得分视为所有岗位都已变强，也不能由八个保留种子推断广泛泛化。
这是一份可复现的初步训练成果，后续应重点解决进攻时机、推进与火力配合。

完整证据：

- [训练报告](tactical-foundation-20261007/report.json)：配置、分项结果、策略内容 hash、种子和二进制 SHA-256。
- [24 个候选的评价记录](tactical-foundation-20261007/evaluations.jsonl)：逐候选权重、对局统计与训练轨迹。
- [最终引擎复核](tactical-foundation-20261007/final-engine-validation.json)：另外 288 局的九个批次全部复现同一 aggregate hash 和胜负统计。

训练 exe SHA-256 为
`c3a8a250bcd988fce8f14d534be24b181dc9332b05f7164d5d32ed492e91f4d0`。
最终验证 exe SHA-256 为
`23da8a2088470dc30c3ef04d2fb09379e57c7cf81130a8c81666108d5f912524`。
两者之间补了 CURRENT 探身事实的连续位置校正、预算耗尽后未评价成员保持 HOLD 的
修复与回归。训练预算 128 高于首版每轮最多 84 次评价，不触发该预算修复分支；
探身标志未进入这三个求解器的评分。保留集复核确认这些变动没有改变所测对局结果。

## 验证和查看入口

Windows native `NativeCodex.ps1 build`、既有玩家 `test` 均通过。
新核心以 `-Wall -Wextra -Werror` 构建，最终 `tactical-test` 为 65,264 项检查、0 失败。
验证包含计划时刻/队伍/代际、默认 HOLD、预算、恢复与弹药、同时致命射击、反向进攻、
同时占领、种子复现、复制推进、掩体方向与实际通行。另以 40 个地图种子和 4–6 人
进行 120 场移除敌人的导航验证，均实际占领；这项只验证导航，不证明战斗强度。
武器分布的独立 168 个情境、约 930 万发采样查询对照最大误差为 0.004841。

最终构建重新生成 216 组靶场数据（两枪、九距离、四目标、三射击模式），输出采样与
解析命中率、每发伤害、实际步长 DPS、TTK 和未击杀比例。
HTML 与 CSV 从原生 JSON 生成，查看过程中不再运行仿真。

本地生成物位于 `tmp/`，未作为源码提交：

- `tactical-range.json`、`tactical-range.html`、`tactical-range.csv`：靶场数据和交互曲线。
- `tactical-attack.jsonl`、`tactical-attack.html`：训练策略对 simple，地图 100、4 人步枪；23.32 秒清除并占领，最后转 DEFEND，hash `ba882862`。
- `tactical-match.jsonl`、`tactical-match.html`：训练策略对 mechanical，地图 10005、5 人步枪；60 秒进攻未完成，防守获胜，hash `24845f53`。

这两局用于查看成功占领和进攻失败的决策过程，不构成额外策略强弱统计。
原生 inspect、Python inspect、HTML 脚本语法与 DOM 交互检查通过；浏览器工具连接
超时，本次没有完成真实浏览器画面的视觉签收。未改动渲染，因此没有运行 GPU 验收。
新增 exe 冻结入口另做一次短时训练冒烟，确认报告及运行命令使用冻结副本。
文档链接、UTF-8 与 `git diff --check` 通过。
