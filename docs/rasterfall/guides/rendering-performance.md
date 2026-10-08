# 渲染性能诊断

> 状态：当前
> 所有者：Rasterfall CPU renderer、GPU Scene 与 world producer 性能诊断
> 最近核对：2026-09-23
> 事实入口：`build/rasterfall --help`、`rasterfall_perf.c`、`src/dev-tests/rasterfall_world_benchmark.inc`

本文用于区分 world producer、角色模型提交、CPU raster 和完整窗口阶段成本。GPU native whole-loop、
bridge 和 physical-device A/B 由 [GPU 验收与诊断](gpu-validation.md)与
[GPU 性能标准](../reference/gpu-performance-standards.md)拥有。

## 先选正确入口

### 程序 Block 常驻几何对照

`tools/gpu_block_retained.py` 在当前已暂存的 Windows 程序上，以
`RF_GPU_BLOCK_RETAINED=0/1` 作同包 AB/BA 对照；先通过 NativeCodex 构建并暂存。
默认比较旧地图轻场景与初始 60 感染体，关闭逐三角形读时钟，记录 1080p 原生帧、
阶段墙钟、主线程 CPU、温度/频率、实体范围及完整程序/资产哈希。检测到其他编译或
游戏进程重叠会拒绝该轮。玩法按实时推进，压力组需同时报告实际存活范围。

```powershell
python tools/gpu_block_retained.py --output tmp/block-ab --rounds 5 --samples 720
python tools/gpu_block_retained.py --output tmp/block-pixels --capture --rounds 1 --cases near0 near60 procedural block-lab
```

`--capture` 使用固定 tick，仅作画面验证，不进入正常性能结论；CPU 分步取整与 GPU
合并求值存在细小差异，不能要求逐像素相同。公开姿态入口
`RF_GPU_POSE_PUBLIC_TEST=1` 配合 `--gpu-scene-pose-test` 包含 Block 的职业、武器、
动作、颜色/顺序与位置误差回归，不要求私有 RF-C01 模型。
资源合同见[程序角色常驻几何](../architecture/gpu-rendering-architecture.md#程序角色常驻几何)。
常驻路径的 `enemy_extract_us` 包含 palette 求值和必要的首次/外观变化 bind 构建；
`enemy_upload_us` 包含 palette 上传。`enemy_geometry_reused` 仍描述旧动态三角形整项复用，
不能用它估算常驻 bind 的命中率。比较收益优先看整帧和完整 `enemy_us`。
本轮限定实测和画面、同步验证见[常驻几何现场](../archive/block-retained-20261007.md)。

### 主线程 CPU 占比与同帧分解

`tools/rf_cpu_profile.py` 在当前暂存 Windows 程序和资产的独立副本上，串行采样旧地图轻场景、
60 敌人压力、前哨站一层及首图车间二层。统一 1080p，正常实时固定步和展示节流；
各场景三轮细分、两轮关闭细分对照，保存原始日志、逐帧值、输入哈希、温度/频率和活动进程检查。
检测到其他编译或游戏进程重叠时，该轮拒绝进入结果；`--resume` 可在同一冻结副本上续跑。
`--coarse` 关闭逐三角形层计时，适合 CPU 占比与大阶段定位；`--source` 可复用已有冻结目录。
本次两组相同快照的结果见[CPU 占比实测](../archive/cpu-profile-20261007.md)，
更细的嵌套计时及其扰动见[详细诊断组](../archive/cpu-profile-detail-20261007.md)。

```powershell
python tools/rf_cpu_profile.py --output tmp/cpu-profile-new --samples 720 --rounds 3 --controls 2
python tools/rf_cpu_profile_report.py tmp/cpu-profile-new/report.json --markdown docs/rasterfall/archive/cpu-profile-local.md
```

诊断组合 `RF_GPU_SCENE_PROFILE_SLOW=1`、`RF_GPU_SCENE_PROFILE_ALL=1` 保留预热后的全部
`SCENE-SLOW`，上限 4096 条，并附加 `SCENE-COUNTS`；关闭 ALL 时仍只保留最慢 16 帧。
全样本 CPU 与帧间隔均使用连续 begin-to-begin 端点，最后一张没有下一次 begin 的帧省略。
累计线程 CPU / 累计帧墙钟表示主线程占用一个逻辑处理器的时间比例；不要将整帧减 GPU、
CPU 分位数除以帧分位数，或各项包含等待的墙钟当作独占 CPU 时间。各细分按相同样本求均值，
保留嵌套关系和未覆盖残差；OS CPU 粗粒度只适合累计窗口，不能解释单张短帧的占比。

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

前哨站使用复合负载和真实战斗组作对照。正式地图的专用诊断工具独立使用，不加入实验场巡检菜单。
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

性能场使用一个固定场地。终端选择负载后“开始采样”，结束或 Esc 取消会清理本轮参与者和组件，
恢复玩家与相机。场地周围世界继续参与渲染；比较前应保持其他动态实验组、灯光、图形选项一致。
操作入口和生命周期见[动态实验场合同](../reference/experiment-labs.md)。

固定组为 1 空场、2 感染体、3 组件、4 复合负载。它们在同一场地中创建正式敌人和组件。

### LIVE 真实负载预设

7 为真实战斗，8 为加入枪手的混战。AI、移动、开火、受伤和死亡使用正常 Game 更新；不为保持数量而自动补员。
记录全程帧时间、实际存活范围和活跃交战帧分布，避免战斗结束后的安静阶段掩盖交战成本。

### 全景巡检与自动化

巡检预设已移除，自动化只运行固定负载和真实战斗，不加载正式任务地图进行巡检。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Fixed -Rounds 3 -OutputDirectory tmp/perf-fixed
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_performance_lab.ps1 -Stage Live -Rounds 3 -OutputDirectory tmp/perf-live
```

默认 1920×1080，辅助检查可指定 `-Width 1280 -Height 720`。脚本检查进程退出、有效采样、参与者清理和资源哈希。
正式性能对照应关闭其他游戏实例，使用同包、同设备、同驱动与相同背景实验组。截图和固定 tick 只用于画面检查。
`RF_PERF_LAB_AUTORUN` 接受 1–4、7–8；旧隔离和巡检口径不再作为可选择的实验。

## 实验园区正常场景采样

稀疏灯调整后两外场镜头的计算机、建筑遮挡与全展示对照见
[前哨站外场成本调查](../archive/outpost-sparse-light-performance-20261007.md)，包含温度限频边界。

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
图元，运行 1200 个固定逻辑步。当前咬击不再自动反推感染者；旧专项中的长反推冷却不影响此规则。
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
