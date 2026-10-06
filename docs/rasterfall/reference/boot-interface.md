# RF Core 启动界面合同

> 状态：已确认设计；当前实现范围与限制以运行时架构为准
> 所有者：Core Host 生命周期与 Game 启动展示
> 决策日期：2026-09-30
> 最近核对：2026-10-04

本页定义启动界面的视觉、交互和信息真实性约束。平台与渲染资源归
[Core Host](../architecture/runtime.md)，世界选择与加载归 Game/session；
[GPU Scene](../architecture/gpu-rendering-architecture.md) 保持独立资源与呈现生命周期。

## 三层入口

1. RF PLATFORM FIRMWARE：黑底纯字符日志，打印实际基础服务初始化结果及耗时。
2. RF Boot Manager：清屏后进入键盘菜单，依次提供 Start Rasterfall、RF Workbench、RF Shell、Diagnostics、Power Off。
3. 所选入口：Start Rasterfall 自动探测当前最佳可用图形设备，GPU Scene 初始化失败时回退 CPU；Workbench 提供可点击的 CPU/GPU Scene 选择；Shell 进入终端环境；Diagnostics 显示实测硬件及服务状态，Esc 返回菜单；Power Off 退出进程。Start、Workbench 与 Shell 的启动命令最后都按所选后端汇入同一 CPU 或 GPU Scene 启动页，不按入口复制装载界面。

RF Boot Manager 默认选中 Start Rasterfall，显示五秒倒计时后自动启动；任意按键或鼠标按下可取消倒计时。方向键选择、Enter 确认，数字 1–5 可直接进入对应选项。RF Workbench 是图形环境，RF Shell 是终端环境。自动启动不要求玩家选择渲染器。

| 入口 | 当前操作与返回路径 |
| --- | --- |
| Start Rasterfall | 自动探测 Vulkan 图形能力，优先选择可用的 GPU Scene；探测不可用或正式 GPU 初始化失败时继续 CPU 启动 |
| RF Workbench | 鼠标或方向键选择 CPU Software、GPU Scene，确认后启动；Esc 或返回按钮回到 Boot Manager；GPU 只在确认启动时验证 |
| RF Shell | 键盘终端提供 `help`、`pwd`、`ls`、`cd`、`cat`、`clear`、`devices`、`boot`、`exit`；文件视图只读且限于 `/assets`；`boot cpu`/`boot gpu` 直接启动，单独 `boot` 打开键盘渲染器选择，Esc 或 `exit` 返回 Boot Manager |
| Diagnostics | 进入时查询 CPU 核数、内存、Vulkan adapter、GPU Scene native present 能力及 Core 服务状态；Esc 返回 Boot Manager；不可查询的硬件值明确显示不可查询 |
| Power Off | 结束启动流程并退出进程 |

Workbench 和 Shell 是独立的启动环境，不启动已冻结的 Desktop/Application 原型；
Start、Workbench、Shell 选定后端后共用 CPU 或 GPU Scene 装载页。
RF CORE 先计时并创建可直接绘制字符的窗口；窗口就绪后立即显示窗口结果和下一项工作。
文件系统、输入、软件渲染器及音频随后逐项初始化，各项完成时显示实际结果和耗时。
固件页不显示 GPU 项；CPU/GPU 渲染方式由后续启动环境选择，普通交互启动无需命令行参数。
全部基础服务完成后，固件页才用分隔线单列蓝色的 Core 初始化总耗时，数值与细分耗时对齐；从窗口创建开始计到基础服务就绪，不计入完成后的倒计时。
Core 初始化完成后显示 3、2、1 秒倒计时；按 Enter 可立即进入 RF Boot Manager。倒计时只在初始化完成后开始，
不逐字延迟输出。Boot Manager 的自动启动倒计时从菜单出现后独立计时。
当基础服务初始化确实变慢时，应保留真实工作状态和耗时；失败信息不能在清屏时丢失。

## 视觉语言

| 范围 | 约束 |
| --- | --- |
| RF PLATFORM FIRMWARE | 近黑底、较大等宽字形，仅用文字和颜色排版；模块名用白色，OK 用绿色，工作中用白色，不使用菜单边框或图形装饰 |
| RF Shell（终端环境） | 近黑底、灰白等宽文字、冰青提示符；布局依赖字符行，不使用卡片式菜单 |
| RF Boot Manager | 五个等高条目、统一编号与右侧类别标签、反白选中行；说明和倒计时固定位置 |
| RF Workbench（图形环境） | 深蓝黑面板、冰青强调色、明确按钮和选中态、鼠标悬停反馈；后端说明与选择操作分区 |
| Diagnostics | 实际 CPU 核数、物理内存、Vulkan adapter、GPU Scene 可用性和 Core 服务状态分区显示；关键状态用颜色与文字共同表达 |
| 状态 | OK 表示完成，WAIT/工作中表示尚未完成，FAIL 表示失败；不能只靠颜色表达 |
| 进度 | 终端用字符进度行，图形界面用几何进度条；两者消费相同任务状态 |
| 动效 | 切页允许自上而下的像素行替换，变化内容允许局部扫描；不持续闪烁，不模拟任务进度或增加等待，界面呈现不改变任务完成时间 |

终端只接受键盘操作。图形界面的渲染器选择、确认和返回均须可以使用鼠标，
并保留可见的焦点、键盘操作和足够的点击区域。图形路径不能只是换色的终端菜单。

### 排版与交互细则

启动环境采用统一的工业控制台视觉：近黑背景、冰青操作强调、灰白正文与低对比单线分隔。
顶栏标识当前环境与输入方式，底栏保留操作提示；标题、分区标签与正文使用不同字号层级。
RF Boot Manager 保留统一编号、反白选中行和固定说明区；五个条目等高排列，默认项与倒计时醒目。

终端按字符行显示输出，命令回显使用冰青，`help` 按命令与用途对齐。提示符固定在输出区下方，
长输出换行，长输入显示末尾与光标；渲染器选择出现时只显示容得下的最近输出，避免覆盖。
图形引导左侧显示目的地与静态 CPU/GPU 芯片示意，右侧集中后端选择、返回和启动按钮。
芯片图仅表示渲染方式，不表示设备型号、工作负载或实时遥测。悬停仅高亮边框，点击才改变选择；
鼠标移入其他选项不会覆盖键盘选中项。

几何、位图字形与鼠标热区共用等比画布变换，非 16:9 窗口居中留边，不单独拉伸文字或点击坐标。
装载页区分已完成阶段、当前工作与真实事件日志；事件保留独立结果列和实测耗时列，
超出可见范围时显示最近事件。失败原因显示在独立底部警告条，不覆盖输入或操作按钮。

### 光栅刷新

Firmware、Boot Manager、Workbench、Shell、Diagnostics 与装载页复用 `rf_boot_canvas` 的
调色、矩形、分隔线、位图字形及等比坐标变换。页面负责内容与布局，扫描合成在绘制完成后、正常呈现前执行。
完整切页按像素行从上到下替换，边界上方为新画面，下方保留旧画面；边界带短暂的低强度细线。
静止页面不叠加滚动扫描线、噪点或闪烁。默认整屏扫描约 240 ms，局部刷新约 80 ms。

Shell 输出行与 Workbench 详情区可以局部扫描；命令回显、输入框、选择态、按钮悬停、数值和倒计时直接更新。
密集更新合并到当前扫描的最新目标，不排队、不反复重置截止时间。失败输出立即显示；扫描期间继续处理输入，
同页操作可立即结束正在进行的整屏扫描，切到其他页则从当前显示画面开始新的替换。关闭请求不等待动效。
窗口尺寸变化时立即显示新尺寸画面并清空扫描历史，分配失败时退化为即时绘制。

当前 Core 服务初始化与 Game 装载为同步调用，任务之间的回调不保证连续帧；这些回调立即显示状态，
不为任务中途的扫描插入等待。Firmware 完成后的现有倒计时持续刷新，共用同一画布，随后扫描进入 Boot Manager。
选择启动后先在当前窗口呈现工作状态；CPU 和 GPU Scene 均沿用该窗口，并以保存的已显示画面扫描进入所选后端的启动页。进入装载页的约 240 ms 扫描完成后开始地图任务；窗口尺寸不同时直接显示新画面。进入游戏仍由真实首帧接管，不把扫描位置解释为加载进度。

## 日志与进度真实性

日志必须来自实际调用的模块或子系统。记录至少包含来源、操作、结果与单调时钟测得的耗时；
可另外保留自启动以来的相对时间。服务复用时写明复用，不再次声称执行了初始化。
不使用预设服务耗时，不用无条件成功文案代替结果检查；完成画面的三秒倒计时可由 Enter 跳过，进入装载页的短扫描只承担页面切换。

### 当前耗时项的计量边界

各行记录调用前后的单调时钟耗时；`OK` 表示该调用完成，`N/A` 表示可选工作不可用，
`FAIL` 表示失败。微秒级状态设置也是真实调用，但不代表设备探测或资源加载。
Firmware 只显示基础服务。GPU 启动页使用独立事件表，只显示本次后端选择、GPU 和 Game 装载事件；
此前的 Firmware 事件保留在进程日志中，不重复显示。CPU 启动页沿用 Core 事件表。
GPU 装载日志使用 `stage=gpu-startup`，与 Firmware 的 `stage=core` 区分。

| 阶段与事件 | 实际计入的工作 | 边界 |
| --- | --- | --- |
| Firmware `window` | SDL 视频子系统、窗口、SDL renderer、纹理与像素缓冲的创建 | 从打开窗口前计到返回；这是后续文字输出的基础 |
| Firmware `filesystem` | 初始化 Core 文件系统状态 | 不扫描磁盘或预读资产 |
| Firmware `input` | 初始化输入状态 | 不枚举物理输入设备 |
| Firmware `software-renderer` | 初始化软件渲染器状态及默认帧预算 | 不绘制游戏帧或加载世界资源 |
| Firmware `audio` | 尝试打开 SDL 音频设备及音频流 | 音频可选；失败仍记录实际尝试耗时并显示 N/A |
| Firmware `CORE INITIALIZATION` | 从窗口创建开始到 Core 基础服务就绪的总墙钟时间 | 包含阶段间的早期画面更新；不是各行之和，不含完成后的倒计时 |
| CPU 启动 `gpu-state-init (CPU mode)` | 将 GPU 服务状态初始化为禁用策略 | 内部事件名为 `gpu-state-init-cpu`；不探测或初始化 Vulkan，成功显示 OK |
| 自动启动 `hardware-query` / `graphics-adapter-probe` | 查询平台硬件；用临时 Vulkan 后端检查图形设备与 native present 能力，再关闭临时后端 | 自动选择 GPU 时将本次实测结果带入独立事件表；手动选择后端不发生 |
| GPU 启动 `native-window-prepare` / `native-window-bind` | 切换既有窗口的软件呈现器，取得并绑定原生窗口句柄 | 不重复创建 Firmware 窗口；分别计时并记录失败 |
| GPU 启动 `gpu-backend` | 调用 `rf_gpu_init` 建立正式 Vulkan 后端 | 不包含此前的窗口 native 准备、句柄设置，也不包含首帧 Scene 资源准备、上传或 present |
| CPU/GPU `session-map-load` | `rf_game_init`：加载地图和 World Content、构建 runtime 投影，并初始化 Game Runtime 的相关状态 | 比单纯读取地图范围更广；不包含后面的 session reset |
| CPU/GPU `world-lightmap-bake` | 调用当前世界光照烘焙并更新 generation | 不代表 GPU 光照资源已提交 |
| CPU/GPU `optional-model-texture` | 尝试读取并解析可选模型纹理 | 缺失时显示 N/A，耗时仍是实际尝试；与所选渲染后端无关 |
| CPU/GPU `outpost-session-reset` | 重建 session、gameplay 与角色初始状态 | 当前事件名沿用 Outpost；显式加载其他世界时仍执行对应世界的 reset |
| GPU `renderer-bind` | 绑定渲染上下文及设置渲染选项 | 不包含后续光照烘焙 |
| GPU `map-preview-load` | 读取指挥桌的三张地图预览 | 与当前 session 地图装载分开 |
| GPU `game-runtime-prepare` | 初始化实验区、终端、玩家设置和剧情存档等运行态，准备 reset 参数 | 不包含指挥桌预览和 session reset |
| GPU `game-services-prepare` | reset 后的运行态与所选网络模式准备 | 普通离线启动不包含网络握手；联机入口按实际路径执行 |
| GPU `sound-assets-load-attempt` / `game-audio-start` | 尝试读取音效资源；配置混音器并启动音频线程 | 前者记录加载尝试完成，不表示全部音效均存在；音频启动不可用显示 N/A |
| GPU `scene-source-freeze` / `scene-world-resources` / `scene-source-prepare` | 音频准备后的运行编排、首帧来源冻结与姿态提取；世界模型资源准备；剩余 draw/layer 输入组装 | 三段顺序计时，不重叠；不包含随后 GPU 预热 |
| GPU `scene-prewarm` | 对冻结场景执行首次离屏资源准备与绘制，并创建两路辅助目标 | 包含上传、蒙皮等待及管线/目标首次开销；由 Scene owner 报告 |
| GPU `scene-first-native-frame` | 预热后的首帧资源准备、native 提交、present 和退休 | 成功才结束启动；不包含预热后启动页刷新 |
| GPU `gpu-startup-total` | 从确认启动、呈现工作状态开始到首个 native 帧成功返回 | 包含自动探测、阶段间画面更新和切页扫描；不包含 Firmware、菜单倒计时和用户停留，不是各行之和 |

自动启动探测失败后写出的 `gpu-fallback-cpu elapsed_us=0` 是选择结果事件，零值不是回退耗时。
Shell 的 `devices` 命令另可按需记录 `vulkan-adapter-probe`，它不属于每次启动的固定阶段。
菜单就绪、后端选择和运行中切换等无 `elapsed_us` 的标记也不作为耗时项。
GPU 首帧准备、预热和 native present 已覆盖；上传字节仍由 `SCENE-PREWARM` 诊断日志提供，
不将墙钟耗时解释为纯 GPU 执行时间。
`SCENE-PREWARM` 的 `prepare_us` 包含主准备和光照 CPU 工作，`lighting_us` 是其中的光照区间；
`gpu_us` 包含该次完整 GPU 绘制，`gi_us`/`receiver_us` 是其中的子区间；`aux_create_us` 和
`actor_pool_us` 分别覆盖两路目标创建与可选备用角色池。管线持久缓存命中/未命中分别比较，
进程从启动到退出的墙钟还包含诊断帧和关闭，不能用来代替 `gpu-startup-total`。

进度表示已完成的实际任务或可测量的工作量。若采用阶段计数，应明确显示“启动阶段”，
不把等权阶段比例描述为资源字节百分比或剩余时间。没有已知总量时显示工作中和当前任务，
不虚构总量。已读入文件、已解码、已提交 GPU 传输、可绘制是不同状态。

GPU 信息遵守以下命名：

- 设备检测结果来自实际后端探测，发现设备与可用性验证分别描述。
- 上传统计明确其覆盖范围和完成语义，不将提交字节数描述为已完成传输。
- 上传量、GPU 分配量、驱动预算和物理显存容量不能混用。
- 当前接口没有提供的指标显示不可查询或省略；不填模拟值。
- 终端和图形界面都只在所选后端确实需要时显示对应资源阶段。

加载成功由必要初始化和首帧呈现结果决定。发生失败时保留原因并提供可执行的返回或重试操作；
不能把失败帧或未完成的资源准备显示为就绪。

## 终端与扩展

终端提供命令帮助、当前目录、文件浏览、文本读取、清屏、设备信息与游戏引导入口。
文件命令应读取实际暴露的文件系统并说明其范围；未知命令、无效路径与不可读取的文件均给出明确结果。
终端是启动环境，不自动开启冻结的 Desktop/Application 原型，也不取得 session 的所有权。

新增初始化工作应在其实际所有者处报告开始、完成或失败事件，并提供可读名称。
新增引导项、命令或装载任务通过各自描述表扩展；两种界面共享状态和操作结果，
避免各自维护一份设备、资源或玩法真值。长耗时任务应在可用边界更新界面并处理窗口关闭。
新增耗时项应在实际调用紧邻处读取单调时钟，名称对应计量范围；可选尝试与状态设置应明确命名，
不能以固定零值或菜单等待代替初始化耗时。复用已有服务时记录复用事件，不再次计作初始化。
扩展 Firmware 服务时同步检查固定行布局和 Core 总计位置；扩展 Game 装载任务时同步更新
`rf_boot_progress` 的完成数与总阶段数；增加事件时检查 journal 容量和只展示最近事件的界面限制。

CPU 的阶段计数覆盖地图加载、光照烘焙、可选模型纹理和 session reset；GPU 另包含游戏音频、
Scene 来源准备、预热及首帧呈现，共八个阶段。原生帧呈现前最多显示七个完成阶段，成功后游戏接管
画面，并写入首帧结果和总耗时，不为展示完成表格额外等待。资源分配量和驱动预算仍未进入引导状态
接口，不能把阶段计数当成这些指标。终端 `devices` 可主动探测 Vulkan adapter。
