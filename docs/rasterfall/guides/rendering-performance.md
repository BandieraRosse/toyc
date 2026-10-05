# 渲染性能诊断

> 状态：当前
> 所有者：Rasterfall CPU renderer、GPU Scene 与 world producer 性能诊断
> 最近核对：2026-09-23
> 事实入口：`build/rasterfall --help`、`rasterfall_perf.c`、`src/dev-tests/rasterfall_world_benchmark.inc`

本文用于区分 world producer、角色模型提交、CPU raster 和完整窗口阶段成本。GPU native whole-loop、
bridge 和 physical-device A/B 由 [GPU 验收与诊断](gpu-validation.md)与
[GPU 性能标准](../reference/gpu-performance-standards.md)拥有。

## 先选正确入口

| 问题 | 入口 | 口径 |
| --- | --- | --- |
| 单个角色资源、pose、skinning、vertex cache、submission | `--character-performance` / `--character-performance-suite` | 角色微基准 |
| material/form-light/raster 消融 | `--model-performance` | 模型离屏基准 |
| 固定五角色并发与 worker | `--actor-performance` | actor 并发基准 |
| Campaign world 与敌人数扩展 | `--render-performance` | world producer + raster 离屏基准 |
| 正常窗口阶段 | runtime `rasterfall_perf` 输出 | begin/scene/enemies/raster/overlay/present |
| GPU native 整帧与 bridge | GPU sampling/metrics 工具 | 物理 GPU whole-loop |
| 前哨站游戏内单轮实测 | 控制终端与结果终端 | 固定视角、真实窗口和玩法负载 |

不要把不同入口的累计计时、wall time 或分位数相加。

正常窗口性能主基准为固定 1920×1080，见 [GPU 性能标准](../reference/gpu-performance-standards.md)。
前哨站压力/LIVE、战斗 Scale、园区、玩家 UI、制造机和旧地图性能脚本默认显式传入该窗口尺寸；
使用 `-Width 1280 -Height 720` 可作 720p 辅助对照，首图 Python 入口使用 `--width 1280 --height 720`。
图形正确性、专用离屏和多尺寸布局检查仍按各自工作流执行；历史数据保留原始分辨率。

## 多层建筑和枪手压力采样

先分别测前哨站复合场景、全展示巡检、战斗实验 19/20 的枪手对照，以及首图 RTS 全景、二层剖切和屋顶。
`gpu_performance_lab.ps1` 与 `combat_lab.ps1` 默认使用 1920×1080 窗口，接受成对的 `-Width/-Height`；
显式传入 `-Width 0 -Height 0` 才跟随程序原生显示默认值。比较时核对实际 `extent`、DISPLAY 日志和呈现模式。
`frontier_station_perf.py --views` 支持 `workshop`、`floor-1`、`floor-2` 和 `roof`，默认仍测入口、全景和能源区。
这些固定首图视角只覆盖进攻早期，不能代表反击阶段的密集战斗。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Full -Rounds 3 -Capped -Width 1920 -Height 1080 -OutputDirectory tmp/pressure-outpost
powershell -NoProfile -ExecutionPolicy Bypass -File tools/combat_lab.ps1 -Stage Scale -Repeats 3 -Width 1920 -Height 1080 -OutputDirectory tmp/pressure-combat
python tools/frontier_station_perf.py --views overview floor-2 roof --rounds 3 --samples 720 --width 1920 --height 1080 --output-dir tmp/pressure-frontier
```

三轮用于初步定位；正式五轮性能签收仍遵守 [GPU 性能标准](../reference/gpu-performance-standards.md)。
此轮实测、分段定义、原始证据与优化建议见 [压力场景调查](../archive/pressure-scenes-performance-20261005.md)。

## 前哨站跨层逻辑卡顿

`RF_STOREY_BENCH=1` 配合 `--logic-test` 只在诊断入口执行三轮正式前哨站的
1F→B1→2F→屋顶→1F RTS 命令，复用正常 session、固定步、规划及碰撞。
每段输出实际 tick 数、逻辑累计/最长步耗时、最长步序号、位置轨迹哈希及候选/试走/扫描统计。
诊断 sink 不读取计时来决定路径；关闭环境变量时不会计时或输出这些样本。
它没有 GPU/呈现阶段，不能用来宣布普通窗口 FPS 或手操路线通过。

```powershell
python tools/storey_performance.py --baseline build-windows/rasterfall-windows/rasterfall-baseline.exe --output tmp/storey-performance
```

先暂存当前 native build；对照 exe 必须含同一诊断入口，并与当前 exe 位于同一暂存资产目录。
`test/gpu-test` 会重新暂存并清空包目录，对照备份应放在包外，暂存完成后再复制进包内。
脚本交替新旧执行顺序，保存 exe/map SHA-256、真实退出码、每轮原始日志和 JSON。
比较最长固定步与累计成本时也报告命令完成 tick 数：分摊搜索允许角色等待规划，不能省略等待
而声称整段移动更快。每版本的重复轨迹须一致；新旧等待 tick 不同，完整轨迹哈希不应强求相同。
本轮证据与原生验收边界见[前哨站跨层性能现场](../archive/outpost-storey-performance-20261005.md)。

## 玩家界面与辅助镜头

`tools/gpu_player_ui_perf.ps1` 在同一原生可执行文件、资产、地图、天空和呈现条件下，比较 player 与
experiment 界面的 FPS/RTS，并采样真实武器预览、通讯显示/收起/关闭、实验区远程通讯，以及 `dual-view` 同时显示剧情和选中单位镜头。
experiment 是同版本兼容界面基线；它不冒充改动前的二进制。比较旧二进制时须另存同环境证据。
脚本按轮反转顺序，预热期通过类型化命令配置状态；采样期不反复覆盖 gameplay 或 camera。
每项记录输入哈希、原始日志、退出码、`SCENE-PERF`、`SCENE-CPU`、`UI-PERF`、分槽 `UI-VIEW`、`SCENE-PREWARM` 和 JSON。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui_perf.ps1 -Rounds 2 -Samples 360 -OutputDirectory tmp/player-ui/performance-round
```

`SCENE-CPU` 是主线程操作系统 CPU 时间，短帧可能受到线程时间记账粒度影响；`prepare` 是准备阶段
墙钟。辅助镜头只统计实际刷新的帧，分别报告 CPU 墙钟和 GPU 时间戳；其同步退休等待包含在辅助 CPU
墙钟中，不能和主帧 GPU 分位数相加。隐藏/关闭要求采样期没有新的辅助帧。诊断 capture 与逐帧审计
在本采样中关闭，GPU lane 必须串行。脚本和环境钩子仅为诊断，不改变正常游玩初始化。

`-Executable` 可选择同一个暂存资产目录中的对照 exe，`-Width/-Height` 固定窗口尺寸。新旧二进制
应交替顺序运行多轮，不能在采样时构建或运行另一 GPU 任务。`UI-VIEW together` 是两镜头同主帧刷新的
采样次数；源切换后的首次刷新允许重合。FPS/RTS、单通讯和双镜头应分别报告，不能把辅助刷新耗时
加到主帧分位数上。

预热是进入地图前的准备时间，不计入稳态分位数；`SCENE-PREWARM upload_bytes` 是该次主准备的上传量，
不代表 VRAM 总占用。观察首次移动卡顿时还需区分动态资源首次出现、CPU 几何生成、管线创建与系统
调度；固定站位稳态样本不能证明所有地图路线都无冷卡顿。最新多轮对照见
[多视图优化现场](../archive/multiview-performance-20261004.md)。

## 首图普通行动的阶段采样

在正常 Windows GPU 进程启动前设置 `RF_FRONTIER_PHASE_PERF=1`，可观察同一玩家进程的
ASSAULT 与 COUNTERATTACK，不生成实体、不驱动玩法、不改变呈现同步，也不自动退出。
关闭该进程时集中输出 `FRONTIER-PERF-META` 和逐样本 `FRONTIER-PERF-SAMPLE`；采样中不逐帧
写控制台或轮询文件。每个 world generation/mission 跳过最初 120 个原生渲染帧；整个进程每个
阶段最多保留最早的 4096 帧，超出数量单独报告。这是有界阶段窗口，不是整场任务的所有帧。

帧间隔在下一次 begin 确定并归属于前一张完成的画面，包含正常 120 FPS 节流。最终没有下一次 begin
的待定帧明确省略。CPU 为当前线程的 OS CPU 时间，GPU 使用既有有效 Scene timestamp，包含天空。
记录实际 active/存活 actor、存活/倒地感染者、任务剩余数、阶段时间、RTS、尺寸、present 模式、
准备/录制/等待成本。实体计数只读取 Game/session，不修改任务或制造规则。

为正常性能证据另运行一次普通玩家路线，关闭 `RF_FRONTIER_AUDIT`、`RF_UI_AUDIT`、capture、
逐层/逐上传细分计时、validation 与游戏驱动。路线验收需要这些审计时，另保留验收进程；不能把
它们的结果改名为正常性能。采样器将诊断、暂停、非 PLAYING、GPU 无效、bridge、capture 和
游戏驱动标为无效；工具也拒绝将有审计的日志计入正常帧样本。GPU 时间为零不作为通过证据。

退出后用独立工具解析实际 stdout/stderr 和退出码，不需要窗口：

```powershell
python tools/frontier_station_perf.py --read-phase-log tmp/frontier-normal-phase/stdout.log --stderr-log tmp/frontier-normal-phase/stderr.log --exit-code 0 --phase-report tmp/frontier-normal-phase/performance.json
```

JSON 保留全部原始样本，并按 world/mission/阶段/尺寸/present/RTS 分组报告有效样本数、实体范围与
CPU/GPU/整帧及各成本的 P50/P95/P99。两个阶段均至少有 120 个有效样本才报告 `both_phases_valid`；
这不代替路线或胜利验收。`collector_us` 的 metadata 值计观察器 begin/end 墙钟（含 CPU 时钟读取、
复制和实体扫描），逐行值只计 end；关机输出不在样本内。记录该开销以评估扰动，不能从帧间隔中
简单减掉来声称无测量开销。Windows 短帧 OS CPU 记账仍可能粗于帧间隔。

首图增援资源预热另输出 `SCENE-ACTOR-PREWARM`：成功/缺 donor/失败数量、启动墙钟、上传字节、
CPU buffer 容量与 queue/fence 次数。六份资源的上传量不是 VRAM 占用；CPU buffer 只包含 actor
bookkeeping 及 upload 数组，未包含 catalog model、分配器与驱动内存。该成本包含在地图启动预热
墙钟内，独立上传量另报，不与原 `SCENE-PREWARM upload_bytes` 混算。
`SCENE-ACTOR-WARM-TAKE` 的实际 frame、ordinal、character、weapon 可验证有限池移交；没有该记录
不推断预热命中，外观不匹配仍允许冷加载。验收需实际普通行动的首次两批枪手进场，分别比较
actor/prepare 长尾与入图耗时，保留 exe/资产/adapter/窗口/present 和有效实体规模。
审计进程可用于确认移交及正常画面，不能替代关闭 audit/capture 的阶段性能对照；固定镜头稳态
不能证明首用长尾消失，也不能据单轮结果声称稳定帧率或总显存减少。

R13 普通输入的完整路线（`tmp/frontier-normal-r13/stdout.log`，2026-10-05）记录 world4 的
`ready=6 missing=0 failed=0`，六个不同 spare 在 frame9203/9302 各三次移交，消耗后剩余零。
稀疏任务审计将两批夹在阶段 9952–10144ms、11968–12144ms，枪手计数 0→3→6，符合既有
10/12 秒节拍；不能把稀疏审计当成每次移交的精确阶段时间。可选资源预热实际耗时 451746us，
上传 43821760 字节、CPU buffer 23768220 字节及 49 次提交/等待；地图总预热 2476230us。
该进程含 audit/capture，只证明真实两批资源匹配与正常路线。随后关闭审计的
`tmp/frontier-phase-ordinary-02/` 采集两个阶段各 4096 个有效样本，正常退出 0；六份资源在真实
10/12 秒帧全部移交，actor 准备为 3.310/4.854ms，而旧进程对应为 237.230/247.598ms。
额外预热为 466.412ms，反击整体 P50/P95/P99 帧间隔没有改善；两个版本的行动、镜头与负载不同，
不能据此宣称整体 FPS、同 seed AI 收益或稳定 60 FPS。完整数据、原始哈希与限定范围归
[首图现场](../archive/frontier-station-01-20261005.md)。

友军标签避让的纯布局工作最多 64×9 候选，比较先前块的矩形测试上界为 18144；移位连接线每块
最多三次 rectangle 发射。该上界不代表实机成本。确认实际密集、分离和倒地画面后，继续观察正常
阶段的 layer/prepare 分位数；截图或审计帧不能作为标签开销的正常性能样本。

## 前哨站游戏内性能实验场

Windows 原生 GPU Scene 单人前哨站的控制和结果终端并排放在性能横路北侧，靠近第三、四列之间的路口，
投影朝向南侧道路来向。靠近控制终端按 E。
实验列表采用中文名称，每项附一行负载与用途说明；基础与园区、实战与地图分为两页，上下键连续翻选。
1 至 4 选择基础、64 敌人、24 组件或复合场景；F2 选择隔离场地 / 前哨站背景 / 完整场景，T 选择
上限 120 帧 / 不限帧，Enter 开始，Esc 可取消测试。默认 ISOLATED、正常限帧。
前四项固定十秒、前两秒预热，结束或取消后返回原站位和视角。上一份结果在启动时清空，结果终端按 E 查看。

ISOLATED 经 session 加载两份同基础场地的专用地图，只按所选配置增加敌人或 crate；
前哨站展示、灯、建筑和碰撞不进入该世界。返回会重建前哨站 session，恢复本地玩家与展示请求，
其他 session 内容重新初始化。OUTPOST 保留原四个场地，暂停所有非测试展示与动态灯，
静态世界和碰撞背景仍属于其环境实测成本。两者不得混作一个基准。
登记、所有权、空间例外和生命周期见[实验区合同](../reference/experiment-labs.md)。

### LIVE 真实负载预设

菜单按上下方向键翻选；数字 7、8、9、0 直接选择下列四项。Enter 开始，Esc 取消，T 仍切换限帧。
四项固定 FULL SCENE，新世界首个成功呈现帧完成资源预热后开始测试墙钟，再跳过前两秒采样。
正常推进 16ms session 固定步、AI、射击、伤害、死亡和动画；不锁血、补弹、
持续复活敌人或保留死亡姿态。它们开始时加载新世界，结束或取消重建前哨站，恢复原玩家、视角、
种子和展示请求；其他 session 内容重新初始化。菜单显示此返回合同。

| 选择 | 负载与完成条件 |
| --- | --- |
| 7 | 四名友军与十六名感染体，沿战斗实验预设 19 的出生、装备、能力，30 秒墙钟 |
| 8 | 同一基线加入四名枪手，沿战斗实验预设 20；与 7 使用同一 1337 种子，30 秒墙钟 |
| 9 | 玩家和四名新建普通 AI 同时沿真实前哨站楼梯走 B1→二层→屋顶→一层；目标高度取正式 surface，五人全部到达才推进下一段；120 秒内完成四段才有效 |
| 0（脚本 10） | 加载正式边缘站点 map/content，保留进攻阶段真实任务；入口、全景、一层、二层、屋顶各 6 秒，正常 RTS 楼层剖切；不代表反击阶段的满规模负载 |

7/8 只复用战斗实验的初始布置，后续死亡、槽位释放和计时沿实机规则，故不与战斗 CSV 的保留死亡姿态
口径混算。结果同时提供存活友军/枪手范围、感染体范围、全程 P50/P95/P99、每固定步逻辑均值/最大值，
以及双方仍有存活单位的交战帧分位数和数量。交战筛选用采样帧当前真值，不承诺维持初始人数；
9 的四段完成证据、导航查询/节点、地面/身体扫描及共享导航等待计数写入结构化日志。
逻辑分段累计覆盖预热和采样的所有固定步；GPU 与渲染阶段只统计预热后的呈现帧。
正常渲染器处理全部世界、灯光和角色，实验不设置额外距离或可见性裁剪。

现场与自动脚本使用同一启动、更新、相机、采样、完成和清理函数：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Live -Rounds 3 -Capped -Width 1920 -Height 1080 -OutputDirectory tmp/performance-live
```

脚本额外检查 LIVE 证据、实际交战帧、全部路线完成、五个巡检段和清理结果，并保存 exe、两份正式地图及
content 哈希。原始结果仍是 `report.json`；CPU 阶段、嵌套准备与 GPU 时间不可直接相加。

### 全景巡检与自动化

FULL SCENE 在原场地保留全部背景、当前展示请求、动态灯、天空和 viewmodel，正常渲染器决定可见性。
5、6 是全景巡检：5 保留当前展示状态；6 临时开启全部已登记展示，电子设备为 600 RPM，结束或取消恢复原请求。
两者固定使用 FULL SCENE，不生成额外敌人。按园区入口、控制计算机侧面、电子区、光照区、大厅天空五个观察点
依次运行，每点 6 秒，前 2 秒预热、后 4 秒采样，总计 30 秒。观察点切换不暂停背景更新。
全局结果之外，每点单独报告帧耗时分位数与准备成本；结果终端按 F2 切换概览/逐视角页。
这些巡检不替代连续移动、战斗或首次进入地图的冷启动测试。
自动新进程的“当前展示”使用正常默认全关状态，报告记录 `lab_mask`；游戏内启动则保留用户已经开启的展示。

结果报告帧间隔平均/中位/P95/P99、敌人存活范围、敌人准备/提取/上传、世界准备、提交等待、
逻辑循环墙钟、GPU 绘制均值与有效样本数、主绘制项、实际阴影绘制数、灯数和阴影图数。
概览另列展示层、角色、快照、动态来源、命令录制和天空 compute；主绘制数使用实际提交值。
GPU 时间与 CPU 重叠；敌人准备包含提取和上传，不能累加这些阶段。
GPU 时间包括阴影及 HDR 绘制/后处理，不包含全部 CPU 和 native present 成本。
正常帧率包含 120 FPS 主动节流，UNCAPPED 移除测试期间的该节流，仍可能受实际 present 模式约束。
敌人数或配置变化、取消、采样容量耗尽会使结果无效。没有有效 GPU 样本时不接受 GPU 均值。

自动入口 `RF_PERF_LAB_AUTORUN=1..10` 在约三十帧后启动同一流程，完成后输出 result/config/stages、LIVE 和逐视角数据并退出。
`RF_PERF_LAB_SCOPE=outpost` 选择环境实测；`RF_PERF_LAB_UNCAPPED=1` 选择无主动节流。
`RF_PERF_LAB_SCOPE=full` 保留背景；全景巡检自动使用该口径。
专项采样脚本先要求已暂存当前 Windows build，逐轮反转顺序，保存 exe/map/content hash、
原始 stdout/stderr/runtime 日志与 `report.json`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Isolated -Rounds 3 -OutputDirectory tmp/performance-baseline
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Interference -Rounds 3 -OutputDirectory tmp/performance-interference
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Full -Capped -OutputDirectory tmp/performance-full
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Panorama -Capped -CompareGeometry -OutputDirectory tmp/performance-panorama
```

Interference 在同一 OUTPOST 场景交替运行背景开启与正式隔离：诊断 `RF_PERF_LAB_INTERFERENCE=1`
开启光照展示并保留非测试展示和地图灯。这是组合背景消融，不能将差值全部归于单独一盏灯或球体。
脚本默认 UNCAPPED，`-Capped` 保留正常体验上限；`-Scenes` 可限定场景。
比较固定窗口、分辨率、设备、驱动、材质/过滤选项与 workload；汇总每轮值和分位数，
不能用单次结果承诺普遍收益。自动脚本检查真实进程退出、完整结果、GPU 有效样本和隔离灯合同。
交互式重跑也可从配置结果查看灯数、阴影数、节流和 present 模式。

`-Stage All` 包含原隔离/干扰组、完整背景组、两种全景巡检和下述 LIVE 实验。
`-CompareGeometry` 在同一构建上交替比较显示几何缓存与 `RF_GPU_SCENE_LEGACY_DISPLAY_GEOMETRY=1` 的直接生成路径，
两侧均关闭 GPU 显示常驻，以保留 CPU 缓存对照的含义。报告按场景及每观察点比较，不能将不同镜头的均值差解释为缓存收益。
`-CompareBackend` 与它互斥，成对比较 GPU 显示常驻、相同姿态/敌人几何复用和静态快照缓存；
参考侧通过 `RF_GPU_SCENE_LEGACY_RETAINED_LAYERS`、`RF_GPU_SCENE_LEGACY_POSE_REUSE`、
`RF_GPU_SCENE_LEGACY_ENEMY_CACHE`、`RF_GPU_SCENE_LEGACY_SNAPSHOT_CACHE` 关闭这些路径。
ID 哈希校验两侧共用，因此参考侧不等于旧版本二进制。`PERF-LAB preparation` 增加展示提取、打包、
上传、组装、帧退休、蒙皮批次、上传字节和复用次数的每帧平均值，脚本写入 `report.json`。

`-ProfileSlow`（运行时 `RF_GPU_SCENE_PROFILE_SLOW=1`）保留启动 120 帧后最慢的 16 帧，退出时集中
打印 `SCENE-SLOW`，避免逐帧控制台输出。begin-to-begin 间隔对应前一帧的实际阶段，包括线程 CPU
时间、提交、acquire、present 和 retire；蒙皮批次计时包含记录及等待，不是纯 GPU 执行时间。
线程 CPU 时间粒度受 Windows 计时影响，墙钟差只能提示等待或调度，不足以证明系统抢占。

默认 slow profile 仍按全进程 `frame>120` 的 begin-to-begin 间隔保留最大 16 条，满槽相同间隔
保留先到行。`SCENE-SLOW-META` 报告实际 scope、eligible_ended/ranked、excluded、selected 及
最后 pending 行未被下一次 begin 排名的边界。行内 `frame` 是进程成功帧，`scene_frame/world`
来自实际成功 Scene 冻结值，mission/phase/phase_ms 在同一 end 保存；下一 begin 不回读新阶段。
`main_query_frame` 来自已有主 query 读取，COUNTER scope 要求与 scene_frame 相同。

显式 `RF_GPU_SCENE_PROFILE_SLOW_SCOPE=frontier-counter` 仅筛选 Frontier COUNTER、phase_ms>=16000、
每个实际 world/mission 的首 120 个成功 Scene 帧之后、PLAYING、非暂停、零 bridge、正有限且有效的
同帧 GPU 行。仍用原间隔排名和固定 16 槽；unknown scope 明示回退 global。该诊断不改变普通
`RF_FRONTIER_PHASE_PERF` 的 first4096 窗口、输出格式或计数。后续感染者几何/容量增长、镜头首次打开
与来源切换仍可能是冷峰；16 秒筛选不保证所有资源预热，也不保证视角或 workload 相同。

`aux_us` 是主帧内整个原有 `scene_aux_prepare` 的 CPU 墙钟，包含缓存绑定、子帧准备和同步等待；
它与 `weaver_us` 嵌套于 `misc_us`，不能相加。`aux_refresh_mask` bit0 是剧情/设备，bit1 是单位镜头，
只在对应 `auxN_after>auxN_before` 时记录成功刷新，正常 delta 为 1。未刷新时 child CPU/GPU 均为
0/invalid；失败尝试可体现在 aux_us，刷新位不表示尝试位。child GPU 有效位还要求退休 query
frame ID 等于本次冻结帧，且微秒值为正有限；GPU0 不充当有效时间。child CPU 含退休等待，
主 gpu_us 不含已先行退休的 child GPU，二者不能相加或用分位数相减。
`enemy_created/extract/upload/draw` 描述既有主视图敌人/程序角色阶段；created 也可能是容量重建。

同一构建和场面下通过正常界面分别显示/收起单位镜头，保存动作、OS 截图、硬件/分辨率/present、
exe/map/content hash、原日志和真实退出码，再按刷新位与帧计数差归因。worst16 是有偏诊断，
不能估计刷新频率、总体分位数或稳定 FPS；正式阶段性能须另跑 slow profile 关闭的普通采样。
关闭 slow profile 时仅保留缓存开关分支和现有 stats 中增加的 88 字节清零/复制，无新增取钟或 query。
`RF_GPU_SCENE_PROFILE_LAYERS=1` 另外逐三角形计裁剪耗时，扰动明显，仅作诊断，不用于性能签收；
正常提取计时包含裁剪。脚本会关闭此开关及逐上传细分计时。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Panorama -Capped -CompareBackend -ProfileSlow -OutputDirectory tmp/preparation-ab
```
`--gpu-normal-scene performance-terminal 0` 和 `performance-menu 0` 可定向检查路侧位置与测试菜单。

## 实验园区正常场景采样

`tools/gpu_outpost_perf.ps1` 测量正常 Windows native 场景，默认覆盖天空/大厅、关闭的控制计算机近景、
电子展区和开启的光照展区；`-AllLabs` 显式开启所有展示作压力对照，并排除会强制覆盖开关的计算机检修镜头。
它默认使用固定 1920×1080 窗口（`-Width/-Height` 可覆盖），保留真实逻辑时钟、
120 FPS 节流和正常 immediate/mailbox/FIFO 选择，不经性能实验场隔离。需要先运行 native build。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -Compare -OutputDirectory tmp/outpost-ab
```

`-ComparePreparation` 与 `-Compare` 互斥，在同一构建上交替比较真实局部包围盒及动态层按格式
紧凑打包；参考侧设置 `RF_GPU_SCENE_LEGACY_ORIGIN_BOUNDS=1` 和
`RF_GPU_SCENE_LEGACY_LAYER_PACKING=1`，其余已有合批、剔除和缓存两侧保持开启。
重点比较实际主/阴影绘制数、上传字节和帧耗时分布；上传下降不等于所有视角的 P95 都下降。
脚本尊重已有 `RF_GPU_VULKAN_VENDOR_ID`；未设置时使用后端正常设备选择，不指定厂商。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -ComparePreparation -Rounds 3 -OutputDirectory tmp/outpost-preparation-ab
```

采样器由 `RF_SCENE_PERF_FRAMES` 显式启用，限用于无 `--frames`、无 `--frame-audit` 的 GPU 固定起点诊断；
预热 120 帧后收集指定数量，结束自动退出。`SCENE-PERF` 报告整个 begin-to-begin 帧间隔分位数、
GPU、天空 compute、阴影绘制及主场景分位数、实际主绘制/阴影绘制和上传量。
`shadow_p50_us/p95_us` 包括阴影深度复制；`main_p50_us/p95_us` 包括主场景、HDR 合成和后处理/HUD，
不是独立的光追耗时。`SCENE-GPU-STAGES` 另列灯表 compute、WORLD/天空合成、透明/特效、
viewmodel、后处理/视频合成与 HUD；各区间属于 GPU 总时间，各自分位数不能相加。
`-CompareArchitecture` 比较软件/硬件建筑查询；`-CompareArchitectureShadows` 比较是否重复绘制建筑阴影，
详见[光照对照指南](gpu-lighting.md#建筑硬件光追与对照)。所有比较轴互斥。
`-CompareLightTiles` 比较保守分块灯表；`-ProfileLights` 使用独立片元计数变体，性能结果显式标为无效，
仅用于工作量定位；`-LightAblations` 分别改变可见性/PCF/BRDF 路径，只作成本线索，
详见[光照成本定位](gpu-lighting.md#分块灯表与光照成本定位)。
暂停、尺寸/present 模式改变、无效 GPU 时间或 bridge 会使结果无效；仍须检查完整进程退出与错误日志。

脚本默认五轮，轮间反转 reference/optimized 顺序，保存参数、exe/map hash、原始日志和 `report.json`。
reference 只通过 `RF_GPU_SCENE_LEGACY_LAYER_COLORS=1`、`RF_GPU_SCENE_DISABLE_LAYER_CULL=1`、
`RF_GPU_SCENE_DISABLE_DRAW_CULL=1` 恢复逐颜色/透明度绘制与完整几何提交，两侧使用相同天空和资源。
这个对照不恢复历史构建、重复环境查询或正常帧日志，不能称作旧版全部成本。比较每视角五个单轮
P50/P95/P99 的中位数，并保留范围。固定帧 `--frame-audit` 使用 FIFO，只适合归因和正确性检查，
不能用其等待时间估计正常无界运行的吞吐。

颜色/逐面透明度合批的像素、深度、近裁剪与屏幕层回归使用 Windows `gpu-graphics-test` 构建目标，
设置 `RF_GPU_COLOR_TEST=1` 后运行 `build-windows/rf-gpu-graphics-test.exe`；此定向入口不执行旧兼容渲染断言。
`--gpu-lighting-test` 另检查主视图/阴影独立剔除与完整提交的像素和深度一致性。

GPU 显示缓存的无窗口契约测试可独立运行，不构建游戏或启动 GPU：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_gpu_scene_display.ps1
```

脚本默认使用 `C:\msys64\mingw64\bin\gcc.exe`（可由 `-Gcc` 指定），产物写入
`tmp/scene-display-test/`。测试调用真实 Scene display packet 代码，用严格的资源替身检查文字变化时
复用、跨块增长、预算、失败释放，以及三角形顺序、世界原点和包围盒；性能结论仍由原生窗口采样确认。

## 角色微基准

```text
--character-performance <model> [warmup] [frames] [repeats] [workers]
--character-performance-suite ...
```

每个实例共享 resource、持有独立 pose。输出规模、hierarchy、skinning、vertex cache、model CPU submit、
raster wall 和 total wall 的 mean/median。`submit` 是模型阶段 CPU 累计口径，不能从并行 wall time 直接
相减。suite 的 optional private asset 缺失应为 SKIP。

## World benchmark

典型入口：

```text
build/rasterfall --render-performance 12 --textures
```

它在 1280×720 离屏表面使用固定 Campaign 内容，比较 near/mid 与 0、10、30、60 个普通敌人；保留地图
actor、static props 和 Campaign fixture，不运行真实波次。每项先预热，再报告指定帧数的均值。

可用诊断消融包括 normal、generic-planar、constant-world、flat-planar、no-planar-v2、legacy-enemies、
no-actors。它们只用于定位，不是正常游戏选项；不同消融可能改变遮挡，收益不能简单相加。

benchmark 在路径 flush 后比较完整 framebuffer/depth hash、差异元素与最大误差。`WORLD-PERF` 的
`*_us` 可能是多个 worker 的重叠活跃时间之和；`raster_us` 才是主线程观察到的 raster wall。

该 frame 包含 world/entity submission 与两次 flush，不含 gameplay logic、begin、截图 IO、present、HUD、
交互和战斗 effects。`WORLD-LOGIC` 只对同一 snapshot 做独立 16ms tick，不代表持续 AI、导航 cache、
session/network 或完整 gameplay 帧。

## 正常窗口阶段

runtime 阶段墙钟互不重叠：

- `begin`：Core frame acquire/clear；
- `scene`：world light scope、世界与 flags submission；
- `enemies`：敌人、队友和 world label submission；
- `raster`：首个 world flush；
- `overlay`：interactables、effects、viewmodel、HUD 与后续 flush；
- `present`：Core end/present。

首个 flush 的 command/pixel/path 明细只解释 `raster`；后续 flush 归 `overlay`。较大的 present wall 也
不能在没有 queue/fence 证据时归因于 present API。历史现场见
[2026-09-14 开销调查](../archive/render-cost-investigation-2026-09-14.md)。

## 旧地图真实时钟与西侧按钮

`tools/gpu_scene_old_map_perf.ps1` 在 Windows package 上自动测量旧 Campaign，默认固定 1920×1080，
保存 exe/map hash、参数、逐帧日志、实际敌人数以及操作系统进程/线程 CPU 时间。
`-Width/-Height` 可选择其他尺寸；报告器按保存的启动参数校验每帧尺寸，旧的未指定尺寸日志仍按 720p 解释。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_old_map_perf.ps1 -Stage Corridor -OutputDirectory tmp/corridor-perf
```

Corridor 比较空走廊、原随机按钮、无 Tank 按钮，默认测试 32、64 敌人，每种同时运行
fixed/realtime，默认三轮、每轮 360 帧，隔轮反序。`-CorridorEnemies 16` 可复跑原单次按钮
负载；PowerShell 内调用脚本时可传数组选择多个档位。显式 normal view 将观察位置设在走廊
`(-32000,0)` 并朝西；按钮场景先预热 60 帧，再用按钮位置的交互相机调用正式 session
交互入口，在第 61 帧连续调用 2/4 次原有每次 16 敌人的按钮，构造同时存活的 32/64 敌人。
这不模拟人工按键之间的时间间隔，不改变正式按钮的生成数量、随机池或出生区域。
玩家可被正常拉拽/击飞；不锁血、不停 AI、不覆盖敌人 HP 或复活已死敌人。
它是可重复的按钮负载诊断，不模拟玩家从出生点走到按钮的全过程，也不包含持续射击输入。

Stationary 提供 near 的低敌人数 fixed/realtime 对照及其他镜头；Auto 使用既有 `--auto`
移动、转向、开火和真实波次；All 运行全部。自然波次不能替代一次按钮整波。
时间口径、相机和实际存活/死亡人数来自 `--frame-audit` 下的 `SCENE-RUNTIME`；帧间隔跨越
完整两次采样点，补充较早打印的 `whole_loop_us`。输出保留冷热转换及首次按钮资源开销。

`gpu_scene_old_map_report.py` 检查完整 native 帧、零 bridge/readback/mixed、地图组件、按钮实际
生成数量及帧号、空预热和敌人种类，输出 mean/median/P95/P99、相机/存活/死亡/AI 范围，
以及按实际活敌人数分组的结果（32 与 64 档分开）。分组只包含未暂停的
PLAYING 帧；初始生成 30 不能当作持续 30。CPU 核占用是同一进程各线程累计 CPU 时间除以采样
墙钟，1.0 表示一个逻辑核，绝不是整机百分比；不把退出阶段停留的末帧日志算入游戏 CPU 窗口。
最忙线程不等于已由 OS 标识的 main thread，必须结合串行调用链归因。原始数据始终保留。

这些有逐帧日志及外部采样的测试是归因工具，不代替低扰动产品 FPS 验收；真实时钟会改变 AI、
死亡和特效演进，不能要求与 fixed tick 的逐帧 workload 相等。现场与优化建议见
[西侧按钮调查](../archive/west-corridor-performance-20260925.md)。

### 共享目标导航场诊断

默认使用共享场；同一 package 的 `RF_GAME_LEGACY_FLOW_NAV=1` 选择旧集团，
`RF_GAME_LEGACY_GROUP_NAV=1` 选择逐敌参考路径。避免同时设置对照开关。
`RF_FLOW_TEST=1` 配合 `--logic-test` 只运行共享场行为用例及正式西侧走廊 32/64 人新旧对照。
必须等待进程实际退出并检查 stdout、stderr 和 `rasterfall.log`，不能只读 PowerShell 的表面退出码。

`FLOW-TEST` 记录到达数、建场、边验证和旧搜索次数；`FLOW-CORRIDOR` 使用固定种子、相同目标与
图元，运行 1200 个固定逻辑步。咬击反推冷却在该专项中固定为长时，以免把到达后被击退计为未到达。
新旧策略的集结等待、出发时间和路程不同，须同时核对全员到达及整段工作量，不能只比较某一静止阶段。
该专项不包含 GPU 渲染，不代表帧率签收。

`SCENE-LOGIC-FLOW` 的 `builds/edges/hits` 为建场次数、物理边验证和连接缓存命中，
`samples` 是共享场/目标连接扫掠预留的有界采样工作量，直达短连接另看 `nav_short_queries/nav_samples`。
`waiting/repairs/evictions/overflow` 分别计没有共享路点而进入局部探索的调用、共享格细分、场淘汰及
节点容量不足。持续移动版本的 `waiting` 不代表敌人原地等待，也不等于实际停步时长。
`work_us` 只覆盖共享任务调度/搜索/细分，不包含全部个体直达与局部移动；它嵌套于 world 更新，不能
与 `world_us` 相加。`intent_us` 覆盖个体直达检查、近追可达候选选择和局部路点接入；共享场的
导航计时为 `work_us + intent_us`，集团对照为 `nav_group_us + intent_us`。这些计时不包含实际
位移碰撞、战斗和分离；读取总成本时仍须检查地面查询及完整 world 更新。

### Game 导航与扫描分段

西侧坡道的逻辑地形对照可在 Windows package 根目录运行：

```powershell
$env:RF_TERRAIN_BENCH='1'
.\rasterfall.exe --logic-test
Remove-Item Env:RF_TERRAIN_BENCH
```

该显式诊断入口跳过普通逻辑回归，使用正式地图的玩法图元、正式西侧按钮的刷怪矩形、
固定种子和全普通追击敌人。对照组只把西侧六段坡道／平台碰撞的高度差压为零，
保留图元类型、数量、顺序、其他地图内容及固定目标；0、16、32、64 人各运行
五轮交替顺序的 960 个 16ms Game tick，覆盖默认共享场追击及后续行进。`TERRAIN-BENCH-TIME` 是未绑定 Game
profile 的完整 world 更新墙钟；`TERRAIN-BENCH-NAV` 来自同初态的第二次运行，
用于解释导航候选、采样和碰撞扫描，不与前者相加。测试要求所有敌人保持存活且
首个敌人穿过坡道。两种地形可能选择不同路径，应先核对实际轨迹和工作量，
不能把总耗时差解释为单次高度计算成本。此入口不测试渲染、GPU 或真实时钟帧率。

独立 Scene 的 `--frame-audit` 额外输出 `SCENE-LOGIC-NAV`，报告工具将可用的
`SCENE-LOGIC-*` 分组汇总到 `report.json` 的 `logic_profiles`；旧日志允许缺少这些组。
`nav_search_us` 包含 BFS 初始化和搜索；`nav_paths_us` 包含沿父链尝试候选目标、
线段障碍检查及逐点地面/碰撞验证。两者属于 Game 更新内的嵌套计时，不能再与
`world_us` 或敌人耗时相加。`nav_paths_max_us` 是本渲染帧中单次父链验证的最大耗时。

`nav_searches/nav_nodes/nav_candidates` 分别计实际搜索、出队节点和候选目标；
`nav_segments/nav_samples` 计路径地形验证调用与实际采样点，含队友直接路径检查。
`nav_ground_queries` 只计父链验证内的地面查询，可与全帧 `ground_queries` 比较。
`ground_scans` 是地面查询遍历的图元数，`ground_heights` 是范围排除后实际求高度次数。
`body_queries/body_scans` 计高度碰撞查询和两个扫描循环实际访问的图元数；
`segment_queries/segment_scans` 计直线障碍检查及实际访问图元数。扫描量包含被标志过滤的项，
`ramp_transition_queries/ramp_transition_scans` 计坡道接缝检查及其实际扫描量。
扫描量不等于相交数，也不覆盖所有玩法碰撞函数。路径计时在函数阶段边界读取时钟，图元循环仅计数。
这些计数仍会扰动执行；关闭帧审计不绑定 Game profile，正式性能收益须另作低扰动验证。

导航采样默认复用同一次高度碰撞查询的地面结果；`RF_GAME_LEGACY_NAV_GROUND=1` 在 Game
帧审计下恢复重复查询及高台范围排除前的坡道扫描作诊断对照。该开关不改变候选路径或碰撞判定，
未绑定 profile 时不生效。`RF_GAME_PROFILE_NO_CLOCK=1` 保留查询/扫描计数并关闭 Game 内部读时钟；
计时附带的 world/敌人调用计数也为零。它可定位读时钟扰动，不能代表
关闭全部计数的低扰动运行。环境变量不由确定性 Game 层读取。

同包交替对照入口如下；每个 workload 紧邻执行复用／参考路径，第二轮将整个顺序反转，
固定 tick 逐帧核对相机、实际敌人种类/数量及导航/碰撞工作量，允许地面查询数量下降。
完整 Game 状态、随机数和事件的逐 tick 对照由 `--logic-test` 中的双随机池用例覆盖。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_old_map_perf.ps1 -Stage Corridor -Rounds 3 -CompareNavGround -ProfileTriangleUpdate -OutputDirectory tmp/nav-ground-reuse-ab
```

`-ProfileTriangleUpdate` 设置 `RF_GPU_PROFILE_TRIANGLE_UPDATE=1`，额外汇总成功的动态三角形
资源更新：`SCENE-TRIANGLE-UPDATE` 的 `validate_us` 包括动态输入校验及 bounds 计算，
`map_us` 包括必要的 staging 容量准备及首次映射，`copy_us` 仅计连续 `memcpy`，
`flush_us` 计非 coherent 刷新及其收尾，`transfer_us` 包括可选 copy 提交/等待及更新收尾。
顺序三角形资源创建时会持久映射 host-visible 顶点内存；无此内存时，首次更新创建并映射保留的
staging。映射在资源销毁时解除，热帧通常没有 map/unmap。`bytes` 是复制字节，
`flush_bytes` 是按设备 nonCoherentAtomSize 对齐后的实际刷新字节；coherent 更新为零。
`direct_flags` 和 `staging_flags` 是成功更新所用分配的 Vulkan memory property 位掩码累积值。
这些字段是 CPU 墙钟，覆盖敌人、程序角色及分层动态资源；
不含新建资源或失败更新，不能直接等同于 `enemy_upload_us`。`staging` 记录采用 transfer
路径的更新数；为零只证明这些成功更新没有 staging 提交。字段关闭时不额外读时钟。
报告的 `triangle_updates` 保留分段、字节数和资源数，`nav_ground_comparisons` 保留每轮配对差值。

## 诊断规则

独立 Scene 的固定场景成本工具不是空地图测试：默认加载 Campaign，保留 AI、展示内容和组件；
`tools/gpu_scene_cost.ps1` 的默认矩阵仍为 0/30/60 敌人。少量敌人问题可用
`tools/gpu_scene_preview.ps1 -Independent -Views near -Enemies 10 -Frames 96`
及 20 敌人档单独采样。预热帧、实际存活/死亡来源数和镜头必须同时记录。
该工具强制每渲染帧一个 16ms 逻辑 tick，不能据此排除真实时钟下 AI 补步的成本；
也不能把固定 near 镜头结论推广到任意地图位置。
旧地图光照与覆盖范围的源码核对和补测见
[2026-09-25 调查](../archive/old-map-lighting-investigation-20260925.md)。

1. 先固定 build、资产、分辨率、camera、seed、tick、worker 和 workload。
2. 先用阶段统计定位问题域，再选择一个独立消融。
3. 同时保存输出 hash、命令/像素规模和 timing；画面或 workload 变化时性能对比无效。
4. 预热后运行多次，报告每轮值与聚合方法；单次首帧不构成收益结论。
5. 改动后先跑最近的逻辑/像素/视觉回归，再按风险扩大到正常窗口或 GPU native。
