# 边缘站点 01：2026-10-05 实现与验证里程碑

> 状态：历史
> 所有者：Rasterfall 首图开发
> 现场日期：2026-10-05（Asia/Shanghai）
> 归档原因：保存本轮阶段证据与未完成签收边界，不作为当前设计或最终完成声明

当前合同见[开发任务书](../reference/frontier-station-01-task.md)，当前优先级见
[活动计划](../plans/frontier-station-01.md)，进入与操作见[玩家指南](../guides/frontier-station-01.md)。
本文记录形成时，正式完整玩家路线与普通 ASSAULT / COUNTERATTACK 性能仍待补齐。
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

证据目录为 `tmp/frontier-normal-r3/` 至 `tmp/frontier-normal-r9/`，另有独立 R3 只读解析
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

记录形成时仍待：在同一次正式完整路线补齐所有 checkpoint、胜利返回及正常退出封口、
实际音频试听及更多同版本普通性能复测。普通 CPU 可玩冒烟已按上述有限范围通过，完整 CPU 任务路线未验证。
本次里程碑不宣布整个任务书签收，也不承诺稳定 60 FPS。

提交前最后一次原生构建与逻辑回归包含 CPU 只读审计和倒地指挥修复，均退出 0。
日志为 `tmp/frontier-checks-downed-command/build.log`（32.751 秒）及
`test.log`（47.850 秒）；SHA-256 分别为
`a49ca8641aedc64ef697e300351b327dfd312176510186c05cc0b4f49e5042b8` 和
`225218ecbfdd01e27e3efe2e7a21fff72bef755377f4e14e60687e7229a08c45`。
该版本 executable SHA-256 为
`6f6e85e7d6a1343dbb9efa184196030f903d1cb8f521bcff6802dd4129cd5857`。
倒地玩家只在离线首图仍正常运行时可选择、移动或停止存活队友；成员身份、碰撞、控制权限和世界代际
继续由原命令链校验，倒地身体不能移动，制造与设施接管仍要求存活玩家。
