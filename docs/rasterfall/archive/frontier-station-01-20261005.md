# 边缘站点 01：2026-10-05 实现与验证里程碑

> 状态：历史
> 所有者：Rasterfall 首图开发
> 现场日期：2026-10-05（Asia/Shanghai）
> 归档原因：保存首图普通完整路线通过、早期失败和剩余签收边界，不作为当前设计依据

当前合同见[开发任务书](../reference/frontier-station-01-task.md)，当前优先级见
[活动计划](../plans/frontier-station-01.md)，进入与操作见[玩家指南](../guides/frontier-station-01.md)。
首图正式普通完整玩家路线已由 R13 首轮、R15/R16 后续及最终版本 R18 闭环通过；
最终 R18 原116动作连续完成、正常返回和实际退出0，战败 R17 同时保留。
普通 ASSAULT / COUNTERATTACK 阶段性能样本另行保留，稳定60FPS、完整CPU路线与平衡未签收。
后续现场必须保留自己的版本、日志与退出码，不能用下述旧样本替代。

## 原生构建与稳定合同

本轮协调者确认统一音频/UI native build 会话 `45001` 与 test 会话 `60165` 均退出 0。
这是协调会话回报的结果；本文不虚构独立持久构建日志。对应随后 R6 staged executable SHA-256 为
`d7a13e2293d0ef366496e010227961f18cd7ceaa9b90f7ffc4c7c74b9a13aa34`。
文档检查通过 310 篇 Markdown，`git diff --check` 通过。

随后玩家共享导航修正的统一 native 验证已形成持久记录：

| 命令 | 退出码 | 持久结果与日志 | 日志 SHA-256 |
| --- | --- | --- | --- |
| build | 0 | `tmp/frontier-checks-nav-final/build.json`、`build.log` | `04b74a4cfdcc7611ddebd8236cb300deb6a3d68da0a636b829d48c22dad10fb9` |
| test | 0 | `tmp/frontier-checks-nav-final/test.json`、`test.log` | `9646595a21f7e6f1706ff355f00aea358fb0b71990a2c9ca2cd3b4db24bcd755` |

这组构建对应 R9 executable SHA-256
`a2dcd89afec5e63e8f8440e66221d9113f73e4d06c19b52d8cf7b8ba7b78b88b`。
回归日志包含 `ACTOR-NAV` 缓存、保守终点、窄门与闭墙/重开检查，以及
`RTS local shared-planning/waypoint/final-goal/single-step/STOP/FPS-fire passed`。
后续为 CPU 只读遥测添加的源码和构建不能倒推属于这组已验证二进制。

任务实现覆盖稳定身份、五人小队与编组、12 名守军、20 秒周期感染者、45 秒整备、固定 36+6
成功增援配额、三设施接管、有限制造供给、领取、失败/重试与返回。
构建及逻辑回归证明稳定合同可执行，不能替代真实门洞通行、三方交火、普通输入和画面签收。

## 地图、资产与共享内容

正式地图的此轮 SHA-256 为
`79785fb6c6867017624c2c30790c164539fbb6144683ac12815ab715822f89bb`。
native Session 冒烟加载 27 个 region、196 个 props、388 个碰撞 primitive；独立步行网格为
117×127，共可达 13200 个格点。12 个守军位置、庭院集合点、北/东/西路线与刷怪入口可达，
三设施终端有可接近位置。25.008 秒固定步冒烟观察到实际周期感染者生成、进入和战斗，
队友移动及射击，`failures=0`。这是 Game/session 与离线通行证据，不是普通玩家完整路线。

地图布局证据为 `tmp/frontier-station-layout/output.json` 与 `output.png`；较早的
`tmp/frontier-layout/` 对应不同地图哈希，不能与最终图混用。
`tmp/frontier-assets/audit.json` 保留 canopy、cargo_rack、fence、collector、rock、bollard
六类新增环境资产的真实网格、材质、边界与哈希，共 896 个三角面；同目录 `model-views/`
保存原生模型取景。资产生成源、注册和地图引用属于实现证据，概念图不是游戏截图。

GPU v2 的 entry、overview、energy 三个 1280×720 原生取景均退出 0，每项 12 个 source audit、
11 个 native audit，validation error 为 0，PPM 与 PNG 在 `tmp/frontier-station-gpu-v2/`。
该 executable 哈希为 `463011da47b4d7a843f2defbf80474b9b5fe34edddffd92527be7afc93b19747`；
它与 R6 不是同一二进制。CPU v1 的 entry、workshop 静态取景退出 0，输出在
`tmp/frontier-station-cpu-v1/`，地图与二进制均早于上述版本。
这些样本证明相应资源路径可渲染，不能证明当前 CPU 普通可玩路线、RTS 室内选择或完整任务通过。

随后普通 CPU 可玩冒烟已完成，限定范围为 `CPU ordinary playable smoke only`。
`tmp/frontier-cpu-ordinary/process.json`、`actions.json`、`report.json` 记录 PID 32476，
executable SHA-256 `1cac9edee176d8d40937c79d9a84273acae49e25f20484db321cee655cbb70cf`，
地图哈希仍为上述正式版本。实际 argv 为 `--renderer cpu --map
rasterfall/assets/maps/frontier_station_01.map --window-size 1280 720`，没有 boot fixture、
帧数限制、固定 tick、GPU 场景驱动或远程控制；只读 audit 与隔离的普通 UI/story 状态启用。
该二进制新增 CPU 只读 audit 路径和实际角色字段，区别于 R9 的构建。

实际状态验证了五名 Lv2 队友、12 名活体守军、队友 AK/AK/AK/AWP/霰弹枪以及 2/3/5 三编组。
普通 M、数字 3 和地面右键命令验证全部队友目标接受与移动；玩家普通 RTS 目标 `(0,4000)`
记录实际到达。首个 20 秒周期波实际生成普通感染者 2 名、快速感染者 1 名，其三个 slot/generation
分别在任务逻辑时间 21008、21184、21360 ms 内越过地图真实站点北界，队友 fire_seq 实际增加。
随后通过普通 Esc 暂停菜单返回前哨，观察 `world=0`，WM_CLOSE 的实际退出码为 0。
客户端 OS 截图 `01-initial-cpu-client.png`、`02-live-cpu-client.png`、`03-outpost-cpu-client.png`
均为 1280×720；复核后两张可见站点 RTS 行动画面及前哨 FPS 返回画面。
报告 `passed=true` 仅表示这项冒烟，仍明确 `full_route_verified=false`、
`performance_measurement=false`。截图的单帧 FPS 文本不是性能样本，不据此宣称帧率达标。

## 普通玩家路线的实际范围

| 运行 | 已有证据 | 未达到的门槛 |
| --- | --- | --- |
| R3，PID 31020 | 普通 Win32 输入；任务四阶段均被观察；固定配额、敌人清空、三设施受控后的合法 SECURED；协调者完成制造领取与返回观察 | 三设施在 PREPARE 内接管未形成完整 checkpoint 证据；独立 parser 未重建协调者完整动作，`full_route_verified=false` |
| R4，PID 28640 | 四个部署条目、站点进入、周期波截图；记录 ASSAULT 与 COUNTERATTACK；正常关闭退出 0 | 输入段失败；PREPARE 接管、制造领取、胜利返回证据不齐，报告不通过 |
| R5，PID 31868 | 部署与 ASSAULT 周期波；正常关闭退出 0 | 输入段失败，未形成完整阶段与设备路线；报告不通过 |
| R6，PID 29852 | 使用统一音频/UI 构建；部署与周期波；普通输入和设施截图；正常关闭退出 0 | 已保存 replay 的成员断言受旧审计 helper 状态影响而报错；不能据此推断普通编组失败，完整路线仍未封口 |
| R7，PID 33680 | 部署、三阶段；协调者在整备开始约 15.5 秒内接管三设施、开始制造并领取；报告包含四个实际操作 checkpoint；正常关闭退出 0 | 最终进攻时玩家倒地、队伍留在能源区，战术未完成肃清；无胜利与返回，`full_route_verified=false` |
| R8，PID 29372 | 普通部署、PREPARE 三设施接管、制造开始及领取 checkpoint；周期感染者和最终进攻截图；正常 WM_CLOSE 退出 0 | 工厂内等待未肃清庭院枪手，后续普通庭院扫荡仍失败；观察到 FAILED，没有胜利与返回；`full_route_verified=false` |
| R9，PID 27516 | 新玩家共享导航构建；普通部署、PREPARE 三设施接管、制造开始及领取；玩家真实到达庭院，三方最终进攻截图；FAILED 下普通暂停菜单返回前哨，WM_CLOSE 退出 0 | 最终进攻未肃清，`counterattack secured` 超时；玩家和五名队友均倒地，仍有四名枪手及两名感染者；自动路线 return checkpoint 缺失，`full_route_verified=false` |
| R10，PID 32672 | 普通 Outpost 启动后正常 WM_CLOSE 退出 0 | 首个输入动作在启动遥测尚未就绪时执行，`audit={}`，`Range approach needs an alive ordinary FPS player` 失败；没有进入任务，没有任务 checkpoint，`full_route_verified=false` |
| R11，PID 33492 | 普通部署、PREPARE 接管、真实制造领取；自然交火在 COUNTER 67.920 秒达到合法 SECURED；玩家倒地时两组真实 RTS 命令及胜利后全队移动接受；普通返回与 WM_CLOSE 退出 0，五类 route checkpoint 齐全 | 操作者制造后约 91 秒没有继续 replay，晚进入 COUNTER；玩家已倒地，动作索引 90 仍尝试身体移动而超时；原错误与 `input_failure_observed=true` 保留，`full_route_verified=false` |
| R12，PID 19932 | 连续 replay 完成部署、PREPARE 接管、制造领取和真实三方进攻观察；协调者继续普通交火，在 COUNTER 311.216 秒达到合法 SECURED，玩家 HP120；普通返回与 WM_CLOSE 退出 0，五类 route checkpoint 齐全 | 可选侧翼动作索引 104 等待两名收件人目标接受时，两人实际倒地，严格断言超时；之后五名队友均倒地，由玩家真实开火肃清末敌；原错误与 `input_failure_observed=true` 保留，`full_route_verified=false` |
| R13，PID 29484 | 同一连续普通输入路线完成部署、周期波、PREPARE 三设施接管、真实制造领取、三方进攻、合法 SECURED、普通返回及 WM_CLOSE 退出 0；116 个动作无错误，五类 checkpoint 齐全，`full_route_verified=true` | 通过范围为正式普通首图路线；不扩展为完整 CPU 路线、任意战术保证或稳定 60 FPS |

证据目录为 `tmp/frontier-normal-r3/` 至 `tmp/frontier-normal-r13/`，另有独立 R3 只读解析
`tmp/frontier-r3-readonly/report.json`。R3 截图覆盖部署、入口、RTS 门岗、仓库、最终进攻、
设备、胜利、制造面板及返回；普通输入、只读任务审计与截图应一起复核。
`valid_secured_observed=true` 只证明观察到合法胜利，不能自动补全未记录的用户操作。
R4/R5 的 `input_failure_observed=true` 与 R6 replay 的
`Group order selection differs from the requested members` 明确保留，不能由 exit 0 覆盖。
R6 该报错属于旧审计 helper 读取状态的假失败，不能视为游戏编组合同失败；需用当时实际画面复核。

R8 保留两次真实 replay 失败：`counterattack secured` 与协调者继续操作的
`ordinary player finishes remaining gunners` 均超时。其 `process.json` 明确
`returned_outpost=false`，关闭成功不能补成返回成功。
R9 `root-actions.json` 记录 FAILED 下 Esc 前后 `paused=0→1`，并保存
`r9-failed-pause-menu.png`；协调者随后用普通暂停菜单返回，日志末段实际观察到
`world=0`、`phase=0`、`paused=0`、三设施与生成计数重置。
`process.json` 记录同一 PID 的 `normal_close=true`、`exit_code=0`。
这些证据证明失败后的暂停、返回和进程退出链路，但没有修改自动报告中缺失的 return checkpoint，
也没有覆盖 R9 的 `input_failure_observed=true` 或失败任务结局。

R10 与 R11 executable SHA-256 为
`6f6e85e7d6a1343dbb9efa184196030f903d1cb8f521bcff6802dd4129cd5857`，
地图哈希未变。R10 的首个动作没有读取到任何启动 audit，不能据此归因于任务初始化或地图玩法失败。
R11 真实制造时间为 PREPARE 14.192 秒开始、16.928 秒 READY、18.560 秒领取、19.152 秒关闭面板；
制造自身并非长时间等待。下一段输入在约 91.3 秒墙钟间隔后恢复，已是 COUNTER 61.312 秒。
倒地玩家对队友 1/2 下达 `(6144,23500)`、对队友 3/4/5 下达 `(6500,29184)` 的动作
均记录 fresh selection、实际目标接受与 `completed=true`；随后尝试移动倒地玩家身体的真实失败保留。
其后正常战斗继续产生胜利，未写死敌人数量或移除活敌。

R11 胜利时已成功生成 36+6 名固定增援、无 pending、无活体任务敌人且三设施受控，
五名队友 HP 为 85/100/120/120/109，玩家仍 DOWNED、HP=0。
`root-actions.json` 记录胜利后普通编组 3、minimap 点击和庭院地面命令，
五名队友 `command=1`、实际 formation 目标靠近 `(0,18000)`、`moving=1`。
`r11-secured-with-downed-player.png` 保存当时画面，说明倒地并不取消存活队友的普通 RTS 指挥，
也不把倒地身体伪装成可移动角色。其最终 `report.json` 同时保留
`valid_secured_observed=true`、五个实际 route checkpoint、`input_failure_observed=true` 和
`full_route_verified=false`；正常返回与退出成功未覆盖旧输入失败。

R12 executable 与 R10/R11 相同，累计周期感染者为 3 名。部署至制造沿连续普通输入完成，
没有 R11 的制造后操作者长间隔；新的失败发生于实际战斗中的收件人生命状态变化。
`actions.json` 保留动作索引 104 的 `Timed out: all selected group destinations accepted`。
选择时队友 1/2 仍存活，等待接受的三秒内均倒地，不能把无效收件人写成全部接受。
协调者随后用普通指令调三名支援队友靠近，两组仍未肃清全部枪手；五名队友最终均倒地。
仍存活的玩家用实际制造 SMG 完成剩余枪手交火，标准弹匣从 40 发消耗到 27 发，备弹保持 0，
`r12-secured-live-player.png` 与 `root-actions.json` 保存 HP120、SECURED、36+6、pending=0、alive=0、
三设施受控的实际状态。最终报告保留合法胜利与全部五类 checkpoint，同时保留输入失败，
不把继续操作后的胜利升级为无中断整路线通过，也未降低收件人验证规则。

R13 使用 focus/prewarm 统一构建，native build 与 test 均实际退出 0，记录为
`tmp/frontier-checks-focus-prewarm/build.json`、`build.log` 和 `test.json`、`test.log`。
日志 SHA-256 分别为 `a8cf104b7646b1c2edc5f7b779c368c89a1efe2ad21d465fa64df38353f8a7d8`
与 `3f34c1ead8da86d243f4f143dd4212bd8e675d2f31a96c5071176101eaf62060`。
本次 executable SHA-256 为
`bcf9c324178cea9db49fe420312d1cf4f601f55b5d7566b04c74920d79f6d74a`，地图哈希未变。
启动 argv 为 `--boot --window-size 1280 720`，audit 为只读，实际记录
`normal_sim=1`、`driver=0`；没有通过任务驱动直接改变玩法状态。

`tmp/frontier-normal-r13/actions.json` 的 116 个动作无错误。三设施受控 checkpoint
位于 PREPARE 14.432 秒；制造在 17.232 秒实际开始，serial=1，22.944 秒实际领取。
此次选择真实手枪蓝图，领取后手枪为 15 发、备弹 0，保留 AK 主槽及有限库存；普通数字 1
重新装备 AK，没有给予制造武器无限弹药。五名队友先通过真实 RTS 指令支援车间，清理感染者后
与玩家移向庭院支援，沿正常射击、换弹与物理移动完成交火。
胜利 checkpoint 实际观察到 COUNTER 37.888 秒、36 名感染者与 6 名枪手成功生成、
pending=0、活体任务敌人=0、三设施受控、victory_count=1，玩家 HP18。
`05-live-three-faction-factory-support.png`、`05b-AK-player-and-five-courtyard-support.png`
和 `06-secured-proof.png` 保存实际行动阶段和胜利画面。

普通 Esc 暂停菜单返回后，checkpoint 观察到 `world=0`、world_generation=6、phase=0，
设施、任务及生成计数重置；`07-return-outpost.png` 保存返回画面。
同一 PID 的 `process.json` 记录 `normal_close=true`、实际 `exit_code=0`。
`report.json` 为 `full_route_verified=true`、`input_failure_observed=false`，
全部五类 route checkpoint 齐全且 `valid_secured_observed=true`。
三份证据 SHA-256 为：report `4b8a8b8e0fef12d1e849d2019ee90bfc51268b2ddcfc3faac39c1ce4acefb360`；
actions `0c34d154fa52fe2c1c1a963dc0c193fb4beb4e84a13b3c049f39d8a29941e826`；
process `8ad80ba6a4d6db10e11748c1eaa34363443f41b6bc548cf9c862f30eb833f4ad`。
该通过没有覆盖或改写 R3–R12 的输入错误、失败任务与部分继续操作后的胜利。

## 导航和混音的局部量化

Game-only native 导航基准使用正式地图、seed 719，每条路线执行 1800 个固定 16 ms 步，
包括到达后的空闲。三次完整总耗时中位数如下，单位为毫秒：

| 路线 | 到达人数前/后 | 调整前 | 调整后 |
| --- | --- | --- | --- |
| 仓库南门 | 5/5 | 15.889 | 6.335 |
| 仓库东门至庭院 | 5/5 | 40.937 | 9.391 |
| 西路枪手进攻 | 6/6 | 26.304 | 16.559 |

最后的闲置过期修正另测为 6.277 / 9.203 / 14.523 ms，终点和确定性查询计数保持。
正向直线路径缓存减少重复规划，物理移动仍做碰撞检查；缓存租期、失效条件及强制重算比较通过。
西路六人仍有成员到达同一点，未宣称解决小队间距或拥挤。
证据在 `tmp/frontier-ai-profile.md`、`tmp/frontier-nav-after-final.log` 及前后基准日志。
该基准不含渲染、开火或感染者模拟，不能转换成实际 FPS。

玩家 RTS 移动随后复用共享导航路径，保留最终用户目标并逐段执行 waypoint；正式固定步
回归通过，R9 普通输入另观察到真实玩家从工厂走到庭院目标 `(0,22500)`，
到达位置为 `(36,22673)`，半径门槛 320 RFU，`actions.json` 动作索引 89（从 0 计）完成。
同次路线的门岗、仓库东西门及能源区至车间分段移动均记录物理到达。
这证明本次玩家路径已通过普通输入与实际碰撞执行，不能将其扩展成整张地图的任意目标通行保证，
也不能以导航通过抵消 R9 战斗失败。

机器原工作循环 48 kHz PCM 峰值 562、RMS 346.352；音乐峰值 1415、RMS 533.904。
在 2560 RFU，原有平方距离衰减使机器剩四分之一音量，可能被音乐遮蔽。
既有单声部部分合成 fallback 已有饱和风险，多个同相声部会放大该风险。
RF 现在以战斗 192/256、音乐 128/256 混音，机器原 PCM 提高两倍；战斗/音乐和机器总线
分别软限至绝对值 28000、4096 以下，独立上界相加低于 PCM16 范围。
资产不重生成，空间衰减、声像和 60 ms 淡出保留；旧 `toy_sfx_render` 行为保持。

27 组旧 API 输出与改动前实现逐字节一致，19 组八同相声部加机器压力样本硬削顶为 0。
每 512 帧混音四轮 CPU 中位数由 27.470 增至 29.686 µs，增加约 2.216 µs，
为 44.1 kHz 音频块预算的约 0.019%；零增益、距离、线程和已有音频合同检查通过。
原始 PCM、比较源码与结果在 `tmp/audio-audit/`，该数据不表示游戏主线程帧成本。
离线检查未打开输出设备；设备启动日志也不能代替实机试听，听感仍待确认。

## 渲染性能边界与剩余签收

GPU 取景中的 47–63 ms actor skin upload 位于冷预热，不是已经证明的逐帧瓶颈。
v2 entry 预热为 1.852 秒、约 140 MB 累计上传，后续 actor 准备多为几十微秒。
仅九个诊断热帧的 interval 中位数约 11.059 ms；frame 10 的约 49.784 ms 尖峰主要是
42.035 ms retire，而 GPU draw 为 2.978 ms。capture frame 12 又包含读回和文件写入。
这些带 audit/capture 的数字不能作为普通 FPS 或优化收益；没有据此改 GPU wait 或恢复跨帧流水。
分析原件为 `tmp/frontier-render-profile.md`，正常采样定义见[性能指南](../guides/rendering-performance.md#首图普通行动的阶段采样)。

普通阶段观察器已实现，跳过每个任务最初 120 native frames，每阶段最多保留最早的 4096 帧，
关机集中输出；GPU query 经当前帧 fence 退休后读取并校验 frame ID，属于同一帧。
下一次 begin 只补上一张画面的 interval，不把新阶段/实体数关联到上一 GPU query。
另一次普通进程 `tmp/frontier-phase-ordinary-01/` 已正常 WM_CLOSE、真实退出 0，PID 33332。
启动仅 `--boot --window-size 1280 720`，环境只有阶段观察与独立普通 UI/story 路径，
无逐帧 audit、Scene capture、固定帧、玩法驱动或 validation layer。
executable SHA-256 为 `1cac9edee176d8d40937c79d9a84273acae49e25f20484db321cee655cbb70cf`，
地图哈希与上述正式版本相同，content SHA-256 为
`5ea024d135d5ccaca169ada369c704fceaa71b6916553456e7bc8ae68a1634d6`。
独立重读确认 8192 条原始样本与 JSON 一致、帧身份唯一、所有 invalid=0、GPU 时间大于零，
四份原始日志/动作文件哈希与报告一致，地图与 content 哈希一致。二进制哈希来自运行与关机报告；
之后统一构建更新了 staged executable，不能用现在的文件重新证明旧二进制字节。
两阶段各 4096 条有效样本，均为 world/mission 4、RTS、1280×720、
present_mode=0（Vulkan IMMEDIATE）。ASSAULT 帧 6956–11051、阶段时间 2.048–60.432 秒；
COUNTERATTACK 帧 24286–28381、阶段时间 0–69.792 秒。更晚帧分别丢弃 9734 / 1100 条，
不能把这两个窗口当作整阶段分布或最高敌人数保证。

| 成本（ms），P50 / P95 / P99 | ASSAULT | COUNTERATTACK |
| --- | --- | --- |
| 实际帧间隔 | 14.213 / 20.201 / 22.668 | 16.783 / 24.283 / 27.981 |
| Scene GPU timestamp（含天空） | 3.434 / 3.838 / 3.991 | 4.164 / 4.711 / 4.899 |
| 主准备墙钟 | 5.694 / 10.909 / 13.121 | 6.697 / 13.565 / 15.889 |
| actor 准备 | 2.856 / 4.196 / 4.777 | 3.405 / 5.189 / 6.048 |
| layer 准备 | 1.588 / 3.637 / 4.276 | 1.633 / 3.329 / 3.980 |
| pose 求值 | 1.470 / 1.817 / 2.070 | 1.506 / 1.831 / 2.042 |
| skin batch | 0.645 / 1.373 / 1.877 | 0.671 / 1.840 / 2.130 |
| 退休等待 | 3.631 / 4.204 / 4.365 | 4.517 / 5.077 / 5.355 |
| 固定步逻辑 | 0.104 / 0.199 / 0.342 | 0.113 / 0.439 / 0.669 |

主线程 OS CPU 均值为 10.124 / 11.726 ms；P50/P95 均为 15.625 ms，
P99 为 15.625 / 31.250 ms。该次 CPU 计时只有 15.625 ms 的离散记账值，
不能将零 CPU 帧解释为无工作，也不能把这些分位数当作精确短帧 CPU 分解。
观测到 ASSAULT 存活 actor 13–17、感染者 0–3、任务敌人 7–13；最终进攻存活 actor 6–12、
感染者 0–15、任务敌人 3–19、待生成 0–30，实际规模与活跃/死亡槽位分开报告。
metadata 的全进程观察开销为 136455 µs；每行 end 开销 P50/P95/P99 为 4/6/13 与 4/6/12 µs。
总开销覆盖 begin/end 与未保留帧，不能除以 8192 冒充每样本总开销。

CPU 为 Windows 11 / Ryzen 5 5600H、12 逻辑处理器。设备日志列出 AMD Radeon 与
NVIDIA RTX 3050 Laptop；默认选择代码优先独显，推断该运行使用 RTX 3050，但现有日志
未直接输出最后选中的 adapter，枚举行本身不能作为所选 GPU 的独立证明。
操作者用普通小地图道路命令完成守军清理并进入反击，动作记录无失败；保存的是 OS 客户区截图，
仍有外部截图负载，且各行镜头/选择并不完全相同，不能称为固定镜头 A/B。

两次最大的反击长帧在阶段 10.000 / 12.016 秒：259.033 / 262.912 ms；
actor 准备为 237.230 / 247.598 ms，上传为 23.831 / 23.678 MB。
同期 GPU 为 5.120 / 4.877 ms、retire 为 5.445 / 5.178 ms，主要尖峰不是已观察的 acquire 等待；
需优先追查实际增援首次 actor 资源准备。正常 acquire P99 为 26 / 24 µs，
此轮数据没有支持先修改 acquire semaphore wait stage。
阶段性能证据已成立，完整玩家路线签收仍独立；准备/skin/retire 有包含关系，分位数不得相加。

### 六份增援资源预热的普通复测

`tmp/frontier-phase-ordinary-02/` 为另一个普通进程，PID22284，实际 WM_CLOSE、正常退出0；
启动仍只有 `--boot --window-size 1280 720`，无 audit、Scene capture、固定帧、玩法驱动或
validation。两阶段各4096有效，invalid 全0、GPU timestamp 全大于零、污染标记0；独立重读
8192条原始样本逐字段等于 JSON，并重算全部分位数、检查帧身份唯一与四份存储日志/动作哈希。
同一帧 GPU/query 合同与01相同。实际 selected 日志直接确认 index1、queue-family0 的
NVIDIA GeForce RTX3050 Laptop GPU；CPU/OS 与01相同，RTS、1280×720、IMMEDIATE。
executable SHA-256 为 `bcf9c324178cea9db49fe420312d1cf4f601f55b5d7566b04c74920d79f6d74a`，
map/content 与01相同；该版本包含预热、导航及 AI 修正，尚不包含后续友军标签排布。

ASSAULT 帧5244–9339、阶段2.208–57.984秒，存活 actor12–17、感染者0–3、任务敌人6–13。
COUNTERATTACK 帧21034–25129、阶段0–71.760秒，active actor18–24、存活6–12、感染者0–15、
任务敌人3–21、待生成0–30。更晚帧分别丢弃8406/1684；这是最早有界窗口，不能代表整场峰值。

| 02成本（ms），P50 / P95 / P99 / max | ASSAULT | COUNTERATTACK |
| --- | --- | --- |
| 实际帧间隔 | 13.573 / 19.468 / 21.758 / 38.621 | 17.501 / 25.513 / 28.558 / 38.372 |
| Scene GPU（含天空） | 3.237 / 3.835 / 3.912 / 3.990 | 4.157 / 5.011 / 5.163 / 5.241 |
| 主准备墙钟 | 5.374 / 10.536 / 12.547 / 19.001 | 7.079 / 14.229 / 16.231 / 25.704 |
| actor准备 | 2.702 / 3.873 / 4.350 / 7.837 | 3.492 / 5.303 / 5.987 / 9.408 |
| layer准备 | 1.480 / 3.416 / 4.095 / 5.125 | 1.677 / 3.413 / 4.061 / 5.189 |
| pose求值 | 1.465 / 1.773 / 2.016 / 3.552 | 1.521 / 1.838 / 2.061 / 2.671 |
| skin batch | 0.645 / 1.237 / 1.834 / 5.717 | 0.675 / 1.861 / 2.128 / 2.537 |
| 退休等待 | 3.365 / 4.205 / 4.293 / 5.324 | 4.497 / 5.393 / 5.549 / 12.471 |
| 固定步逻辑 | 0.100 / 0.183 / 0.376 / 1.333 | 0.118 / 0.494 / 0.729 / 1.525 |

02的主线程 OS CPU 均值9.583/12.127ms；P50/P95均15.625ms、P99为15.625/31.250ms、
max均31.250ms，仍只有0/15625/31250us离散值，不作为精细 CPU 分解。
全进程 collector 为126873us；逐行 end P50/P95/P99/max 为4/5/11/31与4/6/12/45us。
全进程开销包括未保留帧及 begin/end，不能除以8192冒充每样本开销。

world4 的 `SCENE-ACTOR-PREWARM` 记录 ready6、missing0、failed0，额外466412us、
43821760上传字节、23768220 CPU buffer字节、49次queue submit及49次fence wait。
总入图预热2744019us，01为2215536us；上传量不等于显存，CPU buffer不含catalog model与驱动。
六个 spare {0,3,1}/{2,4,5} 在frame21579/21677各消费一次，剩余零，ordinal17–22；
分别是普通AK三份、SMG两份与精英AK一份。实际阶段10000/12000ms，active actor18→21→24、
存活6→9→12、pending18→15→12；无需隐藏实体或人工生成状态。

| 真实枪手批次与进程 | frame / phase(ms) | actor准备(ms) | 主准备(ms) | interval(ms) | 上传(bytes) |
| --- | --- | --- | --- | --- | --- |
| 第一批01 | 24858 / 10000 | 237.230 | 248.303 | 259.033 | 23831220 |
| 第一批02 | 21579 / 10000 | 3.310 | 9.243 | 19.813 | 2287260 |
| 第二批01 | 24961 / 12016 | 247.598 | 250.726 | 262.912 | 23678020 |
| 第二批02 | 21677 / 12000 | 4.854 | 8.858 | 21.167 | 1473180 |

02两批的GPU为5.142/5.145ms、退休5.523/5.487ms，主 Scene queue_us为67/105us；
01对应61/88us。逐行 queue_us 只计主 Scene 提交，不是冷 resource-create 的全部提交/等待数。
启动日志证明49次额外资源提交/等待移到预热，正常帧没有这些资源的首次load/pack/create；
不据此声称所有GPU等待消失。02反击全窗口actor max9.408ms、prepare max25.704ms，未重现
01的237–248ms actor首次创建尖峰，且六次唯一移交记录与减小的上传量支持这项具体改善。

两个普通进程未证明同seed，镜头/选择/输入与战斗负载不完全一致，还包含其他源码变化；
第二批感染者数为01的11对02的12，阶段任务敌人上限19对21。02反击整体P50/P95/P99帧间隔
比01略高，不能把首用尖峰修正解释为总体AI效果、平均帧率提升或稳定60FPS。
OS客户区截图仍有外部负载，性能进程本身不作全路线胜利验收；R13路线证据独立保留。
下一步若继续优化应先复测相近实际取景/动作的热准备长尾：02反击prepare P95/P99为
14.229/16.231ms，actor/layer仍占明显墙钟；先用单独诊断细分，再用无审计普通窗口确认。
acquire P99为22/24us、max74/97us，现有证据仍不支持优先改变acquire同步或引入跨帧流水。

02原始证据 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| stdout.log | `44d77da062b80bd0c57985dbe63e2fafebf7b3879ea43c6eb4099622cc8dad73` |
| stderr.log | `6f2ed69e82599bf27fb3ac898530a0a24d9a835d47804b78d7204daf776ce8a0` |
| runtime.log | `89e3db4ccb73b036ef489a23726bb079d63ee39378df132540d3a528fa9e4a2c` |
| actions.json | `0d8b9cfce67a98fbf6fa64e5e287c9110e76c236cc4ba1c030316e40dcb513b3` |
| process.json | `688027f9158db3b2615990e448ab4e96c47fd3b0dbfc90a24a5e91eeba60c7b1` |
| report.json | `f9fc91a794e8f52ba8028fe76f5a18638879a7faccdf7f497ac5a1e7d5bfed03` |

R13 已补齐同一次正式完整路线的所有 checkpoint、胜利返回及正常退出封口。
实际音频试听及更多同版本普通性能复测仍待确认。普通 CPU 可玩冒烟已按上述有限范围通过，
完整 CPU 任务路线未验证；本次首图路线通过不扩展为任务书全部性能与听感目标签收，
也不承诺稳定 60 FPS。

首图玩法里程碑 `d7e1e2d` 提交前的原生构建与逻辑回归包含 CPU 只读审计和倒地指挥修复，均退出 0。
日志为 `tmp/frontier-checks-downed-command/build.log`（32.751 秒）及
`test.log`（47.850 秒）；SHA-256 分别为
`a49ca8641aedc64ef697e300351b327dfd312176510186c05cc0b4f49e5042b8` 和
`225218ecbfdd01e27e3efe2e7a21fff72bef755377f4e14e60687e7229a08c45`。
该版本 executable SHA-256 为
`6f6e85e7d6a1343dbb9efa184196030f903d1cb8f521bcff6802dd4129cd5857`。
倒地玩家只在离线首图仍正常运行时可选择、移动或停止存活队友；成员身份、碰撞、控制权限和世界代际
继续由原命令链校验，倒地身体不能移动，制造与设施接管仍要求存活玩家。

### 自动小队与枪手停驻的原生验证

新增 Game 自动移动意图独立于 RTS 编组。首图五名友军默认 FOLLOW，六名增援按两组各三名
ASSAULT 绑定；两组集结锚点沿庭院 X 分开，原 actor 部署位置、任务配额和库存保持原值。
显式 MOVE/STOP、已认领救援及接敌决策优先于队形。枪手进入射程内的停驻带后以当前位置为
移动目标，避免朝部署点回退与追击反复切换；守军继续原岗位约束。

`tmp/frontier-squad-native-evidence.json` 对应正式地图、seed719、16ms、每场5000步的
Windows native Game 隔离比较：庭院跟随、南门进入/退出、西门进入/退出均五人到达各自
180RFU 容差内的有效目标；两组枪手六人全部到达，末期最小间距995RFU。所有场景身体
碰撞检查无穿墙、库存未变，静止转头阶段500步落点变化为零。
南门退出曾反复切换纵列153次、仅三人到达自身目标；修复后一次进入纵列、五人到达。
修复以领队局部和最终落点两次完整可用检查为展开条件，预算仍每步总计十次候选探测、
最多两组刷新，不追加 BFS。稳定通用障碍合同也保护这个退出条件。

这份比较关闭其他任务角色以隔离移动，off 使用原个体跟随/进攻，on 为同一批身份添加
分队意图。单次 QPC 只计 Game world update，不含加载、输入、检查与记录；门洞场景成本
也有上升，不能解释为普通帧率或总体性能收益。另一个真实有限弹交火 fixture 记录友军66次、
枪手72次开火及枪手426HP损失；实际自动救援复活通过。

Session 补强检查 `tmp/frontier-squad-native-rts-session.log` 退出0：身体受阻落点拒绝且未写
命令，合法单人 MOVE 到达164RFU，STOP 后100固定步位置不变，其余成员自动意图仍有效。
最初失败 fixture 直接写 Game 命令字段，绕过 Session 验证，日志保留；修正 fixture 没有
修改生产目标验证。多选命令仍逐员验证队形偏移，不能将点击中心受阻等同于所有偏移都受阻。

稳定合同覆盖身份/阵营复用、FOLLOW 锚点权限、领队接替、救援、显式命令和五组预算。
Session 新用例最初一次推进12000ms，误过分批生成节拍；改为普通16ms步后通过，六名配额、
绑定mask63、两组count3和原部署目标断言均保留，失败日志未覆盖。

`tmp/frontier-checks-squad-labels-fixed/` 的统一 build/test/gpu-test 全退出0，分别
27.723/45.397/40.436秒；官方 GPU 检查实际完成120帧。对应 executable 为
`a350a6bbcd497617c3174c6bbdcc9331d6aa9654bfbcac057244b37436edafe5`。
build/test/gpu-test 日志 SHA-256 分别为
`66147c357f2e19e0213fe1024f2fb9d0b25196c408dc6f9408515e6a9564ebd3`、
`957172f1e68eab747f9c8671bf042fdb555c47c7e172ef2bdeab626fecc4daaf`、
`d48f79f627904cc4112edeb9ff9b6e069f1f1202b58cca1333fa090bf6af7555`。
这份构建尚未包含随后 CPU 取消帧修复，完整普通路线须另行记录。

### CPU 标签复测暴露的取消帧问题

`tmp/frontier-cpu-labels-ordinary/`（PID7180）单选玩家超时，结果为 false。
初始 OS 截图显示旧临时感染者实验的 Windows 错误对话框遮住游戏；清除该窗口后保存
恢复现场，从正常菜单返回前哨站、WM_CLOSE 实际退出0。失败记录保留，不改写为通过。
该临时 fixture 已修正池内身份用法并关闭后续错误弹窗；后续六个实验进程全部正常退出。

`tmp/frontier-cpu-labels-ordinary-02/`（PID28472）在任何普通输入前未生成初始审计，
唯一渲染看门狗超时后输出0帧总结并退出。超时后查询已找不到进程；启动 helper 未保留
退出句柄，因此实际退出码未知。原报告的 process_preserved 基于未更新 metadata 的假设，
`postmortem.json` 明确纠正它，失败结果仍保留。
代码链路确认取消帧未结束资源 registry，下一次 begin 因 frame_active 拒绝；该失败还未
置运行错误码。Windows 同时排除了 CPU 首个世界帧预热。后续修复和普通复测独立记录。
两次固定取景虽然分别完成 CPU/GPU 渲染，但未看见友军，不能用作标签排布的画面验收。

### CPU 帧生命周期修复与普通复测

Core 现在按世界 generation 在首个 CPU 帧开始前关闭预算，仅在实际成功 present 后恢复
200ms。取消帧必须等真实 worker 全部退出，再恢复 viewmodel 深度状态、释放资源帧 pin 并
清空命令；下一次 begin 可继续。非取消的 begin/render/present 失败置运行错误并返回失败。
真实 Windows renderer/worker 的隔离合同覆盖首次预热、dispatch 前取消、约200ms超时后
14/14 已开始任务 drain、下一帧复用及错误返回，仅平台窗口 begin/present 使用 mock。

该合同还暴露 Windows 32位 long 保存绝对微秒会越界，使 deadline 变为负值。共享 renderer
的绝对计时链改为 int64_t，原有短时统计和调度保持不变。另一份真实 renderer clock fixture
在 INT32_MAX、UINT32_MAX 两侧和 1e12us 检查199999us不取消、200000us取消，以及预算0关闭；
没有用长时间 sleep 代替边界检查。Linux 辅助环境返回访问拒绝，本轮未完成其构建。

成功 worker 原始日志 SHA-256 为
`9f687feb4ab49af2cf2366a5bf9be9cdb656f49032a77886e66719a1776618bd`，
clock fixture 为 `259301e0b26efe0b9dcf721d5db8389b094abe9ad3bedefce2fc2285a51e0ea7`。
最初32位计时失败日志曾被复测覆盖，现有副本从工具记录恢复并明确标注，原始 raw/hash 未知；
不能将恢复副本冒充原始证据。

`tmp/frontier-checks-cpu-frame/` build/test 实际退出0，耗时27.326/49.785秒；原始日志
SHA-256 分别为 `879630cb911cf93b4033a82e0dfecd0d46a551576ca6cfcb62d963655f3c704a`、
`9a34c77b2ff9c017008f7b7c904f3c54c49cb00d27e16d9d1db2c10a2c4051b6`。
对应 executable 为 `a6e2733bb7eb3a587f40814873cc2f51de4787f7227c60d0ebc221caca6a623b`。

同版本 `tmp/frontier-cpu-labels-ordinary-03/` PID30372 在普通 CPU 路径实际启动，未使用固定步、
帧数上限、命令或玩法写入。正常 FPS W 输入让五名无显式命令队友实际跟随；M、1/2/3 编组、
五人移动及玩家单选到达通过，20逻辑秒感染者出生、三条北侧 station 边界进入和队友开火通过。
随后正常菜单返回前哨站、WM_CLOSE 实际退出0，原报告 passed=true，full_route_verified=false。
本次为可玩冒烟，不包含完整 CPU 胜利路线或性能签收。实际截图仍未显示友军标签，标签画面
验收不能从这些行为检查推导。

### R14 仓库角落移动失败

同版本普通 GPU R14（PID25044）完成部署、周期感染者及初始守军肃清，在整备接管仓库后，
action42 的玩家 MOVE 超时。起点(-12632,17855)、目标(-11600,17400)，实际停在
(-12431,17813)；当时玩家120HP、敌人0，并非倒地或任务结束导致。20秒到达断言保留，
`tmp/frontier-normal-r14/report.json` 为 full_route_verified=false/input_failure_observed=true。
原失败动作、日志与图像保留。随后正常菜单返回前哨站 generation6，WM_CLOSE 实际退出0；
该清理不把失败路线改记为通过。

独立正式地图 Session 复现：两个初始点均可移动到目标，但上述实际停点从三个朝向
重发目标均不移动。身体位置与目标合法，角色位于仓库终端碰撞组件膨胀角外约7RFU，
需核对合法身体点与导航网格连接；不以减小碰撞半径或扩大到达容差绕过。

### 普通感染者目标保留的安全版验证

主机普通感染者采用 generation 绑定的4/5距离平方迟滞，保留500ms扫描周期、原始2400RFU
直达优先、SPECIAL最近目标和既有预算。候选输出先写局部index，避免评分途中覆盖当前身份。
新增29项稳定合同覆盖绑定/候选顺序、明显近目标切换、死/downed/inactive、身份代际与槽复用、
地域/层高、导航代际、四条spawn/init路线、直达范围、SPECIAL及通用墙门；撤去局部index的
alias反例稳定失败22。fresh生产Game/header合同及原short/direct/flow均退出0，原flow的
1/8/64全部到达，无legacy BFS。

清洁窗口顺序运行 baseline/safe 各三轮，六进程均退出0，共21.283秒。每轮14场景、1200个
16ms固定步，正式 Runtime Map 投影196 props/388碰撞体；庭院和车间墙门中1/12/64感染者
追击两名真实存活人形，目标做±80/±200RFU交替，并验证死亡/槽复用。64为超任务数量压力，
没有 renderer/present，不能作为普通整场性能或胜率结论。

| 场景 | baseline → safe（三轮中位） |
| --- | --- |
| 单只车间墙前±80/±200 | 19.2秒未咬 → 14.048/14.064秒咬伤；切换7/37→0/0，field builds2→1 |
| 同上 Game QPC | 总耗时16.271/16.648→12.443/12.427ms；p95 20/22→17/18µs |
| 12只车间±200 | 切换345→2；总耗时111.894→86.483ms，samples6236→18847，首次咬伤3.728→3.968秒 |
| 64只庭院±80 | 总耗时359.887→379.452ms；p95 585→607µs |
| 64只车间±200 | 总耗时603.124→565.648ms；p95 777→640µs |

观察单步峰值143 edges、2084 combined samples、511 local、5 candidate probes，均在原
512/2560/512预算内；legacy BFS/group searches 全部0，RNG不消耗。成本混合，这项改动
改善局部目标稳定性和挡墙追击，不宣称普遍提速。
原始六日志、完整表和源码快照在 `tmp/infected-safe/`；冻结 console executable SHA-256：
baseline `dab75c913a0832f2a88becb4e12d01a1084294f274e77a2c94062815c8ec9e14`，
safe `07c681399522b50ad761390292547dec4a4a82d5f9d2f01127a82b1694d6935b`。
复跑必须独占清洁计时窗口；旧 unsafe 原型数字未用于上述结论。

### 仓库转角修复的局部回归

端点修复仅在原角色直线连接失败时检查2400RFU内最近四个同层、同component格中心的
两种轴向转角，最多增加十六次短段探测，无新增BFS。两段验证真实身体、支撑及层高；
转角和第二段的更宽净空覆盖既有临时路点到达容差，零长度和容差内转角不返回。
没有修改半径、到达门槛、地图或任务配额，正常连接成功时不进入后备检查。

通用合同旧版退出5、新版退出0：合法阻角通行、全程无碰撞、封闭区域外部合法目标拒绝、
无过渡的900RFU高层出口拒绝，以及候选预算均通过。正式地图同代 Game/Mission/Session
复验中，R13/R14原起点各三种朝向仍到达；精确停点原先三个朝向20秒内均不动，新版均由
原Session 250RFU到达门槛清除命令，末距214–226RFU，诊断实际退出0。fixture指定起点，
关闭其他角色与任务阶段以隔离移动，不作普通窗口、完整任务、QPC或FPS结论。

旧/新通用日志 SHA-256：`6c0bce957f38a141d27d523f7430fa061ea4ddf86096c9a6b74d68af8ea1e24f`、
`cb611d62e414257e131882a2ec7757e0c5211e91fe7e8ceb64520a837a402dac`；
旧/新正式Session日志为 `d116d0c37f275ae5b609888034278442dc8cc7208a998260e7d998b9b489fb85`、
`919f40dbb4c112163b6ada2544a9a18aa2c2f3ee71318815a314b69b8192f5ba`。
完整源码与场景 receipt 在 `tmp/frontier-r14-corner-receipt.json`。

另一轮探针混用了旧Game对象和新Session翻译单元，启动后没有正常输出，已作废并核对自有
executable后终止PID26544；工具返回-1，Windows原生退出码未知。终止后残留WER对话框，
CPU04初始OS截图明确显示它遮住游戏，异常为0xc0000094；不能将“无存活进程”等同于
窗口已清理。root保存截图并只对精确测试标题发IDOK，随后普通小地图取景显示五名队友
标签与血条错开排列。CPU04仍为失败，恢复标签观察和正常返回/实际退出0另存receipt，
后续普通复跑使用新证据目录。

### 合并后的统一检查与 CPU05

`tmp/frontier-checks-ai-nav-aux/` build/test/gpu-test 均实际退出0，耗时
28.174/44.434/40.829秒；官方GPU检查完成120帧。对应 executable SHA-256 为
`deb768830757d43e880df06c27ae44d0c6690b774cfc10fafa89d7c4d0712587`。
三个原始日志 SHA-256 分别为
`ce16cbe604016c00f7f2e477fedd35fb47c08337d7821b7f4df333a023e957c9`、
`c8682e882e36cb14071c2c41784068fe24473df41eaf431409f17e73e7107383`、
`269832566aa7a5a3c1c0c3297df3f4851bbc470cb46b331feedb53de61f35335`。
这份版本包含自动小队、枪手停驻、感染者身份迟滞、仓库转角、CPU取消帧和64位计时、
增援GPU预热、共享标签及可选AUX诊断。

同版本 CPU05 PID20392 独立普通可玩冒烟全部通过，正常返回前哨站及实际退出0。
`tmp/frontier-cpu-labels-ordinary-05/report.json` passed=true，full_route_verified=false，
performance_measurement=false；仍不扩展为完整CPU任务或性能结论。
`02-live-cpu-client.png` 实际显示五名队友名称/血条错开排列；原CPU远门和水平朝向门与
RTS高俯视镜头不相容，现仅显式RTS的友军非BASE标签放宽这两门，保留10800RFU水平距离、
近裁剪和真实投影边界。敌方、BASE和网络标签门保持原行为。

### R15 同版本正式路线通过

合并版本普通 GPU R15 PID32584 连续116个动作全部完成。部署、20秒周期感染者、肃清守军、
整备内三设施接管、真实制造开始/领取、36感染者与6枪手反击、合法SECURED、返回前哨站均有
同进程checkpoint；没有固定tick、玩法driver、远程命令或状态写入。三设施在整备12.512秒
全接管；反击37.792秒、总逻辑119.984秒观察到captured111、配额36+6、pending0、敌人0、
victory_count1。玩家此时倒地但仍有存活队友，按既定规则合法获胜；不冒充全员存活或平衡签收。

随后正常返回前哨站、WM_CLOSE实际退出0。
`tmp/frontier-normal-r15/report.json` full_route_verified=true、input_failure_observed=false，
五类checkpoint齐全，对应与CPU05相同的 `deb768830757d43e880df06c27ae44d0c6690b774cfc10fafa89d7c4d0712587`。
原动作未绕过R14失败点、未放宽到达门槛。自然倒地现场已保存，中心等待救援提示与近处标签
存在交叠，标签画面还需单独评估；完整路线通过不覆盖这个展示问题。

| R15原始文件 | SHA-256 |
| --- | --- |
| stdout.log | `04a31ca6b093cdc197cb7a94ff1c30c3871c0368dc8cb5af27087ab7161fa224` |
| stderr.log | `6f2ed69e82599bf27fb3ac898530a0a24d9a835d47804b78d7204daf776ce8a0` |
| actions.json | `c5d6b74056329d167563b51988eaa85b017a97eb94c8c1a23cddd6d27ed4ba1a` |
| process.json | `3db63a1aec273d731a6be5ef9264f5abc2af408259ede11929d09e13bf07c66e` |
| report.json | `fcfbf94cd45de0ef5dbb31e8cd412bfebd5bed4ba6a81aeb90667dc68ec37fb6` |

### 单位镜头开启的真实 AUX 归因

已提交的 `86706cb` 版本以同一 `deb7688…` executable 普通键鼠启动，开启
`RF_GPU_SCENE_PROFILE_SLOW=1` 与 `frontier-counter` 筛选，关闭 phase observer、审计、
Scene capture、固定 tick 和玩法 driver。这是带诊断开销的最慢16帧归因，不能用于总体
分位数或稳定FPS签收。第一轮RTS切换后过早点击遇到鼠标仍锁定，原失败sequence保留；
正常按键、等待解锁后的恢复路线独立记录。它不作为完整行动或相同负载对照。

`tmp/frontier-aux-ordinary-open-01/` PID10160实际WM_CLOSE退出0，主帧身份、COUNTER至少
16秒、每个世界至少120成功Scene帧、子镜头刷新计数及有效GPU查询合同通过，无GPU错误。
真实OS画面显示单位镜头开启，2752个合格interval中保留最慢16条：15条刷新slot1，
一次slot1 CPU wall为6.349–8.977ms、GPU为3.095–3.843ms，AUX几乎解释这些帧的misc；
另1条未刷新帧interval35.890ms但AUX仅2µs，不能把全部长帧归给单位镜头。
父视图weaver仅30–194µs，现有证据不支持将机器准备当成AUX主因。CPU scope包含等待，
与GPU时间不能相加；最慢16条的刷新占比不能估计通常刷新频率。

原始stdout SHA-256为 `adbef2021441de4976e9b47404fc54ab214b6daf8dad3d486b3765df555cbc53`，
stderr `6f2ed69e82599bf27fb3ac898530a0a24d9a835d47804b78d7204daf776ce8a0`，
runtime `1cd09c51e28cd690ea14a6388a2adad3ba25ae5b831e706baf972aa616aa5ed3`，
actions `02e185f6487a7dcdd14d91cecfb1ba2aa832a4deabbca7f8cd236bffb9a3b053`。
同目录diagnostic-receipt与parser结果保留实际进程、设置、环境及过滤范围。

底栏收起的独立 `tmp/frontier-aux-ordinary-collapsed-01/` PID29828也实际退出0、无GPU错误、
16条诊断合同通过。真实OS图确认单位镜头隐藏；3268条合格interval选出的最慢16条全部
refresh mask0，slot1 before/after恒为1038，CPU/GPU/valid均0；AUX总scope为0–1µs。
这验证收起时不刷新、不重复归因缓存耗时。仍有23.511–27.555ms长帧，五条有enemy resource
created；收起还会改变单位镜头附近的敌人来源保留，两个窗口的种子和负载未对齐，不能
据此宣称总体帧率提升幅度。stdout/runtime/actions SHA-256依次为
`a1b0764b566911c826e9773f0b79fe74eb414cef746b2a6cb133edbf79386559`、
`eaef426ff0eca71990531957d98827aa675deeffa03038a0e40d9a3e4e48c7c2`、
`52249803a495a52c0768efb52cfedb240c8a94742553f8b8657af633079472aa`；stderr与开启轮相同。

源码核对确认child总GPU包含独立天空、相机相关三张太阳阴影和世界绘制，并同步退休。
父阴影矩阵与child视线不同；现有计时不能细分child阶段，不在本轮盲目共享这些结果或降低
刷新率、质量。机器资源借用没有足够主因证据，暂缓。

### 合并 AI 后的普通阶段采样04

同一 `deb7688…` executable 普通窗口，关闭slow、逐帧审计、Scene capture、固定tick与玩法
driver，仅使用只读 phase observer。采样03实际退出0，但COUNTER只有3644有效帧，不冒充
4096；独立04延长普通等待，PID31000实际退出0，ASSAULT/COUNTER各4096有效帧、invalid0、
startup contamination0，1280×720 RTS、immediate，显卡仍为RTX3050 Laptop。

| 04独立采样，毫秒 | P50 | P95 | P99 | 最大 |
| --- | --- | --- | --- | --- |
| ASSAULT interval | 13.159 | 19.749 | 22.718 | 43.903 |
| COUNTER interval | 17.667 | 25.018 | 28.885 | 42.205 |
| COUNTER prepare | 7.138 | 13.855 | 16.007 | 25.297 |
| COUNTER actor | 3.701 | 5.243 | 6.170 | 9.364 |
| COUNTER GPU | 4.454 | 4.838 | 4.900 | 4.947 |

COUNTER actor18–24、alive6–12、infected0–16、mission enemies4–21、pending0–30。
它与旧窗口的种子、相机时序和存活负载未对齐，不作整体A/B收益；反攻P95大于16.667ms，
稳定60FPS尚未签收。采样不包含后来WAIT提示源码。collector总95416µs覆盖完整观察窗口及
丢弃记录，不能简单除以8192当平均开销。`tmp/frontier-phase-ordinary-04/` 的stdout、runtime、
actions SHA-256分别为 `a769b28d7394f2f6d2ae4d2b8f67b6d42cb282cc0f1d1d89c0cdbb2b5339e5c3`、
`ed2903656d4734db6c4453c2b095d4781821ea925b02cce3bc3f499b04eac36f`、
`a4a2187d3c44c0a2605b6146226454d4ef3c8873d33b6cbf8d52f6550faf8066`；stderr与AUX轮相同。

### 提示板避让、小队公开查询与 R16

HUD提供等待救援板的唯一几何，CPU/Scene友军标签整体避让，普通无提示仍使用原九候选。
共享字体/布局333通用合同通过，四真实编译单元syntax通过；完整日志在
`tmp/frontier-wait-panel-contract-20261005-081312/`，run SHA-256为
`ac5406bfde962a9a4e54c1437f5d1f28301203c16f75d014f003640304dd2411`。
三次临时合同缺链接/签名的编译失败已分别保留，没有运行，也不是生产源码失败。
公开小队goal查询增加FOLLOW anchor ID/generation即时校验，防未tick的同阵营槽复用读旧goal；
内部AI路径和预算不变，既有小队合同及三个身份边界断言通过。
枪手展示名缩成守卫枪手、精英守卫、增援枪手、精英增援；权威身份与配额不使用名字。

`tmp/frontier-checks-wait-identity-names/` build/test/gpu-test实际退出0，31.852/52.812/44.916秒，
官方GPU检查120帧；新 executable SHA-256为
`dbf18509547758181015120191d415a49f4ec53dc9689a9eac273c7622094628`。
原始日志SHA-256为 `8231c1502d5fa1d8ed8a902793b5360220de5a0827c3e68f715cccf33ffb54c5`、
`6955e1986a0cba4d07203c5e3336981d376e9c0f1a32ffebc3fe4488821967cd`、
`c1b77ca117fa7b354db8b9a1575525e4f8e6f80f099ddb4971f64637f51981c9`。

R16 PID2392以原目标连续跑至肃清，再做胜利后的独立自然救援观察，随后正常返回前哨和实际退出0。
正式116动作齐全，report为full_route_verified=true、input_failure_observed=false；不是另一条
绕路。`06-secured-proof.png`显示自然倒地玩家和友军，姓名/HP/DOWNED完整避开中央提示。
额外救援MOVE只发一次，友军真实到点frame18720，首个玩家ALIVE100HP审计frame18990，
逻辑时间相差3008ms；相邻审计有128ms间隔，不伪称精确复活tick或wall UTC。
原观察宏要求距指定world点180RFU，但点击反投影目标偏约57RFU，友军在真实目标161RFU处
停止，距指定点208.5RFU，因此宏到达timeout。这个失败addon完整保留；它没有阻止真实
救援成功。之后只读新鲜身份、500RFU附近位置及截图观察独立通过，不再发MOVE/STOP。
同一玩家id0/gen1恢复100HP，友军保留命令；`08-natural-player-rescue-observed.png`留存。

随后一次请求CPU的boot全路线尝试按已确认Start Rasterfall合同自动选择GPU，因此不算CPU
证据。该窗口在动作34仓库前移动时玩家倒地，到达timeout，保留原失败；正常返回/实际退出0。
`tmp/frontier-normal-cpu-full-01/`名称只是请求意图，stdout明确selected backend=gpu-scene。
仍只有之前CPU05的有限普通冒烟通过，CPU完整行动与CPU性能结论未扩展。

### 最终敌人稳定资源槽与原生检查

敌人冻结来源使用已有 `source_slot` 索引 GPU 容量和完整几何缓存，避免较早来源消失后，
后续未变身体因压紧 draw ordinal 而重新提取、搬入容量不足的资源并重建。程序角色仍位于
敌人容量之后的独立 ordinal 区域，不把槽当永久身份；范围、严格递增顺序、完整冻结值、
world generation、顶点格式和光照模式继续校验。透明与 draw 顺序、裁剪、玩法均不变。
主视图和 shared_parent 使用相同索引，借用不转移所有权，所有 reader 退休及完整槽清理
沿原路径。没有增加数组、跨帧在途资源或新的 GPU 同步模式。

真实源码叶函数合同在 `tmp/frontier-enemy-slot-20261005-082608/`：旧版退出99，删除较早
小身体后错误重建后续大身体；修复版退出0，同一未变来源提取0、创建0、资源指针相同。
稀疏来源、最大数量、越界/重复/倒序拒绝、透明零 alpha、增长/缩小、shared_parent 借用、
live reader 退休、代际失效、缓存关闭和完整关闭清理均通过。几何/驱动接口是显式 mock，
该合同不替代物理 GPU，也不是普通性能测量。旧/新 run 日志 SHA-256 为
`ded42384b62d6e6969333374e7e5ee97ba71baad695b4f385572f46a49ee45aa`、
`95e87693df2489fba1c3c0c2d008784bf979452b211c8d1e2d3b260455868781`。
四个真实翻译单元语法检查通过，临时编译失败亦保留，没有包装成通过。

稀疏槽可保留更多当前闲置资源；既有143槽、每槽4096三角形的顶点/索引理论 payload
上界约40.22MiB/owner，还需另计 staging、cached runs、对齐、纹理及描述符。这不是实际
驻留量；代际改变使键失效，不保证释放所有容量。不以本合同宣称整体 FPS 或显存收益。

`tmp/frontier-checks-enemy-stable-slots/` 最终 Windows build/test/gpu-test 实际退出0，
耗时27.467/45.367/40.226秒，官方 GPU 完成120帧；最终 executable SHA-256为
`afa2f50a5d3bfd1945935ca5cda60d9c6e55de5984c9358d104671aa9952fa8a`。
三个原始日志 SHA-256为
`56fd88652ffeb191bfd3ca2b09550561cbb01e04134f15121643dc4bad32d31f`、
`5937afd9fad8ce77db9b0daff540bb94f9940342c57b2c328c5fa7cf60dea93e`、
`a06090f7ff635a59cfc67f0c0dd158431631acf16a77ad831b0e5b68f7c589fd`。

同版 `tools/gpu_scene_play.ps1 -Stage Combat -Frames 240` 三个串行原生场景完成：
west-death100帧、effects-capacity4帧、continuous240帧，子进程与脚本均实际退出0，
证据在 `tmp/frontier-enemy-slots-combat-01/`。它验证真实 GPU 战斗生命周期与容量压力，
是固定步诊断，不作为普通路线、实时帧率或 validation layer 已开启的结论。

West 实际死亡来源由16逐步移除至0，continuous 进入实际活波；SOURCE/NATIVE frame ID
都连续且 bridges/readback/mixed 为0。effects 实例2048与1交替，属于效果容量压力，
不冒充逐敌人身体容量测量。三个 stdout SHA-256依次为
`23f2abee7eb33348c38fcdcc35ee0e731328fc5f468f5c93f1245a47e0664534`、
`ef88f17096695fd4043a309c7fb6c33c6e19c6b814defb40b57c3520d7be05b1`、
`13e86c65a7e4f6f0c46cce7618f45c3279809c69d7d90e1544d8d53ff1849d47`。
汇总日志没有逐槽资源指针/容量，不能用它声称旧上传峰全部消除。

### 最终版本普通路线 R17 的合法战败

同版 R17 PID32916保持原116动作的目标和到达门槛，完成部署、周期感染者、整备接管、
真实制造领取及全数36+6反击增援。反击39792ms、总逻辑116752ms时六名友方全部倒地，
剩四名敌人，任务合法进入FAILED；没有渲染或 GPU 错误。肃清 expect 因此超时，原失败
动作、审计和画面均保留，不将合法战败改写成完整路线通过。
随后单独执行原返回菜单，正常回前哨并WM_CLOSE实际退出0。
`tmp/frontier-normal-r17/report.json` full_route_verified=false、input_failure_observed=true。
这轮实际包含主视图与开启的单位镜头，以及来源增减和世界返回；它不是完整胜利验收。
此前 R15/R16胜利记录仍各自有效；战斗路线不是相同种子与wall时序的确定性宏，胜败波动
属于本轮未签收的平衡范围，不能用后续复跑抹去失败。

### 最终版本 R18 连续正式路线通过

同一最终 executable、地图和原116动作的普通 R18 PID27056连续完成全部操作，没有
玩法driver、固定tick、远程命令或状态写入，没有放宽目标/到达门槛。三设施接管、真实制造
开始/领取、36感染者与6枪手全数入场、pending0、敌人0、captured111与victory_count1齐全。
反击39760ms、总逻辑121984ms观察到SECURED；玩家倒地，五名队友中三人存活，HP为
89/120/115。它是合法胜利，不作全员存活或平衡签收。

正常返回前哨并WM_CLOSE实际退出0；`tmp/frontier-normal-r18/report.json`
full_route_verified=true、input_failure_observed=false，五类checkpoint齐全，116动作无error。
root直接审阅真实 `frame-000011.scene.ppm`：五名友军标签、倒地状态和血条整体避开等待
提示，开启的单位镜头正常显示。路线包括主/子视图、敌人来源变化和世界返回；没有借此
宣称逐槽借用命中率、显存变化或完整同步验证。R17战败仍单独保留。

| R18原始文件 | SHA-256 |
| --- | --- |
| stdout.log | `d75b2c4f69aa7ccc8832aa3907d3654437723ca44d4c1065b731b65f215a4adc` |
| stderr.log | `f7670e17c91a55d516a1de9729c1c7245d979255cf54f7d8b63c2f5b35740450` |
| actions.json | `a71224808f122116d10003597d8e00218e285539f777aabfda662734a9b0270c` |
| process.json | `6ba942d50c76aac211449f9a57782191b7f888326208a3df54716d4e42116766` |
| report.json | `c88184512be266219f7878498caa534e292cdbecd6a1147870e795bba509e137` |

该战斗复跑不等同于相同种子、相同wall时序的性能A/B。R17只读审计显示玩家先到庭院时
队友仍落后约4.8–5.9k RFU；最后一名队友倒地才触发FAILED，感染者已清、仍剩四枪手。
AK换弹期间仍有充足reserve，不是总弹药耗尽。操作指南补充同步推进和等待支援建议，
单次失败不量化胜率、不据此改配额或削弱失败条件。

### 最终版本普通性能采样06与收尾边界

`tmp/frontier-phase-ordinary-05/` 启动后过早送入普通W/E，鼠标仍被捕获，脚本安全取消
点击。root保存真实OS入口图并WM_CLOSE实际退出0；两阶段均0样本，报告器拒绝缺失样本，
没有通过或帧率结论。没有继续复用该目录；06在同版新窗口把启动等待增至30秒，后续
普通键鼠序列相同，关闭逐帧审计、Scene capture、slow诊断、固定tick及玩法driver。

06 PID28524正常WM_CLOSE实际退出0，无GPU错误；只有只读 phase observer及两个有记录
开销的OS截图。1280×720 RTS、immediate、RTX3050 Laptop，ASSAULT/COUNTER各4096
有效帧、invalid0、startup contamination0；COUNTER记录逻辑0–66.912秒的首个有界窗口。
actor18–24、alive4–10、infected0–18、mission enemies6–24、pending0–30。

| 06最终版本独立采样，毫秒 | P50 | P95 | P99 | 最大 |
| --- | --- | --- | --- | --- |
| ASSAULT interval | 12.768 | 18.363 | 20.771 | 29.222 |
| COUNTER interval | 16.327 | 22.620 | 25.242 | 71.881 |
| COUNTER prepare | 6.814 | 12.954 | 14.503 | 62.522 |
| COUNTER actor | 2.919 | 4.244 | 4.910 | 56.102 |
| COUNTER enemy | 1.329 | 2.847 | 3.304 | 17.920 |
| COUNTER GPU | 4.063 | 4.336 | 4.462 | 4.649 |

反击P95仍大于16.667ms，稳定60FPS未签收。该窗口与04的种子、镜头时序、友军存活及
其他负载不同，不能把分位数差异归因于稳定槽或AI，也不能给最慢actor帧虚构具体原因。
collector总92977µs覆盖完整观察及丢弃记录，不除以8192伪称平均开销。
最终同版普通行动与性能证据各自成立，不用性能序列替代完整胜利路线。

06 stdout/runtime/actions/report SHA-256分别为
`ba2d567b50375d4de8f5f4c3dbe38c17f1eb4d2b9550fa6c12d5f7c740a9c93b`、
`b05e5fd4ab526af0ac9228ca915db7db574a168f60cee3e95bb3d5e4264aada4`、
`c069540a0dc75a5bcdb6cee0ed42822e21b61d7a340073fcbee8a4d634e30547`、
`8d2d7b3b6e46541bdef6ebc3f69fe39f7932fe500e937f6d38ebd49e044eb42a`；
stderr与此前清洁窗口相同。最终地图/content哈希未变化，原始输入三文件保留未提交。

本轮完成首图普通闭环、六类Astra工业资产、任务设施与真实制造，以及有预算的小队、
目标身份迟滞、转角寻路和渲染资源/帧生命周期改进。完整CPU行动、稳定60FPS、听感和
战斗平衡仍是后续验收边界；没有扩展无限波、联网任务或跨帧GPU工程。
