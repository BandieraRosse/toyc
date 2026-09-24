# GPU Scene 固定渲染地图

## 实验性单人入口

构建 package 后可从原生入口启动：

```powershell
.\windows\NativeCodex.ps1 run --gpu-scene-play
```

它自动启用 required native GPU 与独立 Scene，默认进入前哨站，使用正常游戏输入和时钟。
可通过 `--map` 选择 Runtime Map；目前只允许单人。资源复用和性能优化尚未完成，
此入口不代表产品帧率或完整架构签收。Console/GUI 仍受正常运行 feature gate 限制。

`tools/gpu_scene_play.ps1` 检查真实单人启动、窗口输入与 resize、世界切换、连续战役帧和五类
present fault。使用新的 `-OutputDirectory`，可通过 `-ValidationLayerDirectory` 加载验证层；
日志必须证明 validation/sync 已启用。固定 UI capture 可选择 `ui-pause`、`ui-scoreboard`、
`ui-shop`、`ui-over`、`ui-won`，这些只证明内容布局，窗口交互由上述专项单独检查。
可用 `-Stage Interactive/World/Combat/Faults` 定向复跑；默认 `All`。Combat 必须在日志中看到
真实波次的存活敌人，单纯达到帧数不能通过该项。

独立预览还检查每帧 `SCENE-LAYERS`，native 日志应为 `world_only=0`；旧 WORLD 对照仍为 1。
`-Capture` 捕获同一分层批次，包含天空、世界、透明、特效、武器和 HUD。新增 `enemy-fade`
与 `frame-effects` 镜头分别检查死亡渐隐及远端/本地枪口、射线和爆炸输入；其余固定镜头沿用原语义。
层序/透明遮挡/武器独立深度/overlay 无深度/错误层序原子拒绝还由 `gpu-graphics-test`
中的 `SCENE layered depth/blend/preflight` 用例覆盖。该测试是离屏正确性证据，不代替 native 生命周期。

## 独立来源开发预览

### 动态资源运行成本

正式队员不变 bind 上传的同包 A/B 使用 `tools/gpu_scene_cost.ps1 -Experiment BindUpload
-DenseComponents -Frames 32 -Rounds 5 -OutputDirectory tmp/scene-bind-upload-ab`（实际命令写在同一行）。
`RF_GPU_SCENE_LEGACY_BIND_UPLOAD=1` 恢复逐帧 bind 上传。`-DenseComponents` 从正式
Campaign 地图生成测试用副本，保留 134 个 object（其中 113 个组件）的种类、数量和非组件记录，
仅将组件排入 near 战斗镜头周围；输出地图和原始日志保存在本次新证据目录。报告逐帧要求 134 个
object 来源、至少 100 个实际组件 draw，并核对两侧敌人、draw 和组件提交序列一致。
60 敌人档使用 `near-heavy` 固定镜头，其中六个 Tank、六个 Charger 与其余敌人同帧出现；
报告核对这一组成。该镜头与局部组件密度共同构成高负载压力场景，不替代正式地图的自然分布或
低扰动 FPS 签收。

P1 使用 `tools/gpu_scene_cost.ps1 -Experiment P1 -OutputDirectory tmp/scene-p1-ab`，同包交替切换
角色蒙皮批量提交和分层 GPU 资源复用；CPU 工作区复用在两侧均保留。单项归因分别使用
`-Experiment SkinBatch` 和 `-Experiment LayerReuse`，每次选择新的输出目录。
`RF_GPU_SCENE_LEGACY_SKIN_BATCH=1` 恢复逐 mesh 同步蒙皮；`RF_GPU_SCENE_REBUILD_LAYERS=1`
恢复逐帧分层 GPU 重建。工具清除其他成本实验开关，并在结束后恢复全部原值。
`SCENE-RESOURCE-COST` 验证提交次数与资源复用；批量等待计入总 `actors_us`，逐 actor 的
`upload_skin_wait_us` 热帧只计输入复制和排队，不能将它直接当成完整蒙皮成本。
固定内容比较使用相同镜头及 capture frame；resize/world-cycle/fault 和 validation/sync 另行验证。

敌人几何缓存使用 `tools/gpu_scene_cost.ps1 -Experiment EnemyPrep`，连续 draw 绑定复用使用
`-Experiment DrawBind`；两项分别指定新的 `-OutputDirectory`。对应诊断开关是
`RF_GPU_SCENE_LEGACY_ENEMY_PREP=1` 和 `RF_GPU_SCENE_LEGACY_BIND=1`，分别恢复逐角点计算及逐 draw 绑定，
不改变几何或 draw 顺序。采样前清除其他实验开关，只改变当前对照变量。
`SCENE-SUBMIT-COST` 分别记录 native 提交/呈现与退休墙钟，报告同时保留 GPU draw 时间；
不能将整个 submit/present 段解释成 fence 等待。缓存正确性用同帧 capture 的 PPM 字节一致性检查。

队员上传 A/B 使用 `tools/gpu_scene_cost.ps1 -Experiment ActorUpload -OutputDirectory tmp/actor-upload-ab`，
保持动态三角形资源复用，两侧均关闭蒙皮批量提交，分别为临时 staging 与直接写入/保留 staging。工具仍运行三轮交替、每轮 64 帧，
报告均值和分位数；耗时分布用各段累计时间除以累计帧墙钟，不能用中位数占比。
`RF_GPU_SKIN_LEGACY_UPLOAD=1` 恢复旧上传分支；`RF_GPU_SKIN_STAGING_UPLOAD=1` 强制走保留 staging 的备用分支，
用于设备可直接写入时验证备用路径。二者只作诊断，性能采样不要启用强制 staging。
`SCENE-ACTOR-COST` 按 actor 输出 load、CPU pack、upload/skin/wait 墙钟；最后一项仍包含驱动及同步等待，
不是纯 GPU skin 时间。它也会记录补充模块化角色，汇总时应按所属来源解释。

完成 package 后运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_cost.ps1
-OutputDirectory tmp/scene-cost-ab`（命令写在同一行）。工具在同一包、固定 tick、near 0/30/60 下交替运行
重建/复用，各三轮 64 帧，排除前 8 帧，保留日志、exe 哈希及 `report.json`；逐帧 draw 和来源数量必须一致。
`RF_GPU_SCENE_REBUILD_DYNAMIC=1` 是显式诊断对照，仅强制重建敌人/程序角色资源，不改变几何；正常运行不设置。
脚本自动恢复此环境变量。`SCENE-FRAME-COST` 的计时边界见 [GPU 架构](../architecture/gpu-rendering-architecture.md)。
该采样有逐帧日志，属于优化归因，不替代阶段 5 的低扰动五轮 FPS 签收。

`gpu_scene_preview.ps1 -Independent -Capture -CaptureFrame 4 -Frames 4` 可捕获复用多帧后的同批次画面；
默认仍捕获首帧。分别在重建与复用模式运行相同固定镜头，可以比较 PPM 像素并验证资源更新后的实际内容。

### 来源与分层验证

先完成 Windows package，再运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_preview.ps1 -Independent -OutputDirectory tmp/scene-independent -ValidationLayerDirectory tmp/scene-validation-tools/mingw64/bin
```

`-Independent` 选择 `--gpu-scene-independent-preview`，沿用 required native 与 normal-scene/wave-repro
输入限制。无验证层时省略 `-ValidationLayerDirectory`，但不能宣称 validation/sync 通过。
脚本检查每帧 `SCENE-SOURCE` 的连续 ID、零旧 producer/RasterCmd/mixed draw，以及 native 提交零 bridge/readback。
此入口直接冻结地图、正式模块化队员、旗帜、投射物和交互物，以及敌人、程序/网络角色、downed
和补充模块化角色。`dynamic_sources_pending=0` 仅表示动态来源接通；首版分层与完整性边界见
[活动计划](../plans/README.md)，不能报告完整可玩帧或性能收益。
默认独立矩阵另覆盖特感、死亡、舌头和程序角色，逐帧核对来源数、实际 draw 和冻结帧 ID。
加 `-Capture` 会在每个用例首帧用同一冻结 Scene 批次额外读回 `<view>.capture.scene.ppm`，
再进行 native present；该帧报告 `readback=1`，其他帧仍须为零。此图证明 Scene 批次内容，
不作为 swapchain 自身颜色或完整帧性能证据；未加 `-Capture` 的运行仍逐帧要求零读回。
旧入口保留下面的 producer 捕获诊断；新功能按 [活动计划](../plans/README.md)接独立来源。

## 硬件 Scene WORLD 原生预览

阶段 2 的整图差异按用户决定延期修复，不再阻塞该入口。先串行完成 Windows package，再运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_preview.ps1
```

脚本默认验证 near/mid/thin-far（30 敌人）和 Campaign，各四帧；可用
`-ValidationLayerDirectory tmp/scene-validation-tools/mingw64/bin` 启用本地 validation/sync。
CLI 为 `--renderer gpu-compute --gpu-required --gpu-native-present --gpu-scene-world-preview`
加 `--gpu-normal-scene near 30` 或 `--gpu-wave-repro`，可带固定 tick 和帧数。
此入口只显示 WORLD：不透明地图、角色、敌人、附件及既有 WORLD 附属表现。
它不显示天空、透明、特效、VIEWMODEL 或 HUD，不是完整产品候选。
`SCENE-NATIVE` 必须逐帧连续，且 `bridges=0 readback=0 mixed_execute=0`；进程必须退出 0。
普通 `FRAME-AUDIT`、mixed capture、vertex diff、RB0 whole-loop 统计不能冒充该路径证据。
结构回归使用日志和真实退出码；既有 CPU/mixed 图像只用于记录差异。

## 阶段 2 WORLD 整图对照

先完成 Windows package，退出构建后再运行 GPU 矩阵，禁止在运行期间重建 package。
`tools/gpu_scene_stage2.ps1 -WorldOpaque` 设置仅捕获帧生效的
`RF_GPU_CAPTURE_WORLD_OPAQUE=1`：mixed 保留全部 WORLD opaque，黑色背景，截在透明层之前，
并去掉屏幕覆盖。Scene 同帧 PPM 本身只包含 WORLD。此开关不启用正常 Scene 呈现。
near、mid、thin-far 各运行 0/30/60 敌人；`-Views campaign` 使用正式 Campaign 的
fixed-tick wave workload，运行 320 帧并捕获第 320 帧。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "& ./tools/gpu_scene_stage2.ps1 -WorldOpaque -OutputDirectory tmp/stage2-world -Views near,mid,thin-far,campaign"
python tools/gpu_scene_stage2_report.py tmp/stage2-world
```

报告依赖 numpy，输出完整 reference/Scene/四倍差图、并排预览、RGB 分位数、覆盖差异及逐帧
提取/准备/上传/draw/GPU 时间。颜色边缘带仅用于定位，不能代替几何/深度边缘或遮挡证明；
报告明确将深度遮挡差异标为未测。脚本运行 PASS 只代表进程、捕获及审计通过，报告始终保持
UNAPPROVED，最终批准依据[画面合同](../plans/gpu-scene-visual-contract.md)。
`instances` 是 `SCENE-EXTRACT` 的角色/敌人提取项数量，不是完整地图实例总数。
准备时间包含资源创建/同步等待，GPU draw 时间不含 upload、skinning 或 readback。

> 状态：当前输入；显式 WORLD preview 已接入 native present，默认完整呈现仍为 mixed；正常帧审计另行离屏提交 WORLD
>
> 事实入口：`rasterfall/assets/maps/gpu_scene_render_fixture.map`、`rasterfall/src/rasterfall_options.c`、`rasterfall/src/rf_game_runtime.c`
>
> 最近核对：2026-09-23

这张独立 V1 空间地图供固定画面和逐帧审计使用。`--map` 只覆盖本次进程；普通启动仍加载
`rasterfall.map`。地图没有 `attr.identity`，沿用 Campaign 的 World Content policy，因而
正常帧仍会出现 HUD、队友、viewmodel 等非地图内容。地图本身不声明这些玩法对象。

## 阶段 2 剩余 WORLD 诊断

更新 Windows package 后运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_stage2.ps1 -ValidationLayerDirectory tmp/scene-validation-tools/mingw64/bin
```

前者显式选择程序角色、死亡、Smoker 舌头、near 0/30/60、mid 和 thin-far，保留同帧 mixed BMP 与
Scene WORLD PPM、每帧 draw/上传/提取/时间戳以及 executable 哈希。`actor-procedural` 在 Campaign
原有四名程序角色之外新增四名职业 fixture；不替换产品地图。死亡 fixture 覆盖不透明旋转，渐隐计数
归后续透明层。`--gpu-scene-pose-test` 检查职业、动作、武器、重复提取与失败；逻辑回归检查 legacy、
死亡变换、阴影和舌头枚举。

当前网络迁移只运行 `--logic-test` 与 `--gpu-scene-pose-test`：后者直接调用 host/guest 的角色表现入口，
覆盖倒地、排除本地玩家、断线、未激活、只读冻结和几何提取，不打开 socket 或 GPU。
`tools/gpu_scene_network.ps1` 保留为 GPU 完成后网络专项的复现工具；其 guest 数量断言尚未通过，
数量预期本身也需核对可见性。它不属于当前收尾门禁，见[收尾记录](../plans/gpu-scene-stage2-handoff.md)。

Scene 审计仍是同步 readback 诊断；动态资源逐帧预备，敌人/程序角色资源已按退休后的容量复用。GPU draw timestamp 排除 upload/skinning/readback；
prepare 是包含资源操作的墙钟，geometry 是敌人/程序几何提取，local_pose 是正式 roster 独立 pose 提取。
冷帧和后续帧分别保留，短诊断不作为产品 FPS、完整帧性能 A/B 或固定画面基线审批。

## 地图覆盖

| 稳定 ID | 内容与检查点 |
| --- | --- |
| `fixture_wall`、`opaque_box` | 不同面向的墙与实体 box，检查 opaque 遮挡和深度 |
| `fixture_ramp`、`fixture_platform` | 坡道和高平台，分别由 `map-ramp`、`map-platform` 镜头观察 |
| `air_gate_box`、`air_gate_platform` | 同一 `air_gate_fixture*` role 家族；开关同时控制半透明 box、顶面和对应碰撞 |
| `fixture_label`、`fixture_sign` | LABEL 是旧屏幕像素文字；SIGN 有世界几何牌面和世界文字平面，不能按同一种深度合同判断 |
| `near_edge_box`、`thin_far_wall` | 近视口边缘和远处薄墙，用于观察裁剪与远距细线 |

空间地图将可见 `render` 与必要的 `collision`/`surface` 分开记录。除地面和 air gate 外，
其余项目只承担渲染输入，不暗示玩法碰撞。`air_gate_fixture` 当前启动时启用；固定镜头
`map-gate-on/off` 在主循环前通过 map 开关设置对应状态。

## 固定镜头与预期

保留 map-ramp、map-wall、map-platform 的原镜头。新增镜头固定 tick、seed 和帧数；相机高度
仍由正常 session 派生，不能以改变高度来掩盖 Draw 偏移。

| 镜头 | 相机 X/Z；yaw sy/cy | 应见内容与遮挡 |
| --- | --- | --- |
| map-ramp | 0/0；0/1024 | 中央坡道低端接地，高端遮住后方 gate 下部 |
| map-wall | 0/0；-384/949 | 左墙与 box 遮住后方世界内容 |
| map-platform | 0/0；384/949 | 右侧平台只有水平面；近 box 遮住右侧部分内容 |
| map-label | -1250/-1000；0/1024 | WORLD_LABEL 完整可读，屏幕注记覆盖世界，不读写世界深度 |
| map-sign | 1650/-1000；0/1024 | WORLD_SIGN 牌面与字形完整可读，二者参与世界深度 |
| map-gate-on/off | 0/-1000；0/1024 | 相同相机；on 的半透明 box/顶面在坡道后方，opaque 坡道遮住下部；off 两者消失 |
| map-near | 280/170；0/1024 | 近 box 穿过 near=64，仍有连续可见面并遮住后方；不得反转或产生巨大错误三角形 |
| map-thin | 2900/-1500；0/1024 | 远墙形成窄线/窄楔；平台遮住交叉部分，墙不得整体消失 |

`model-legacy` 镜头使用 Campaign `rasterfall.map`，相机在 -2600/-9900、朝向 0/1024，
对准 style 1 方块人展示模型；它不属于上表的渲染 fixture。
`model-special` 镜头使用同一地图，相机在 4900/-10100、朝向 0/1024，
对准 style 3–5 特殊感染体展示模型；它也不属于渲染 fixture。
`model-infected` 镜头在 9500/-10100、朝向 0/1024，对准 style 7/8 的导入感染体展示模型；
同地图的 style 10/11、13/14 也进入同一 Scene world 资源。
`actor-standard` 和 `actor-assault` 镜头分别在 0/5200 与 14000/-2200、朝向 0/1024，
对准 Campaign 两支正式模块化小队的四名队员；两镜头均使用完整 Campaign 地图。
`projectile` 镜头在 0/-3400、朝向 0/1024，固定提交有纹理 bomb、闪烁纯色 bomb 与 molotov；
两帧均走正常玩法更新，随后冻结当帧投射物值供 Scene 审计。
`pickup` 镜头在 300/-10300、朝向 0/1024，观察 Campaign 武器桌上的模型拾取物及程序几何；
正常帧值帧包含 45 个交互物，Scene 绘制 7 个模型项和 38 个程序项。

## 批量与单物体对照

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_fixture.ps1 -OutputDirectory tmp/scene-full
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_fixture.ps1 -OutputDirectory tmp/scene-map -MapOnly
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_fixture.ps1 -OutputDirectory tmp/scene-single -MapOnly -Isolate
```

脚本串行等待 CPU/native GUI 进程退出，检查退出码、文件与 native audit；保存 CPU PPM、GPU BMP、
日志及输入 SHA256。标准库 Python 生成 PNG 原图、绝对 RGB diff 与 `diff.json`，记录变更像素数、
逐通道 MAE/max、原图哈希；不设置容差，不自动批准基线。`-Views` 可在 PowerShell 中传入镜头数组。

`-MapOnly` 在输出目录生成副本，显式使用 `attr.identity=return_to_whu_v0` 的空 World Content policy，
排除 Campaign 队员、旗帜及展示内容；HUD/viewmodel 仍保留。它不改原地图，也不签收角色覆盖。
`-Isolate` 保留 ground 与镜头对应的一种 render（gate 保留 box+platform），重排连续 legacy index，
保留其他空间/碰撞记录。副本及哈希随原图保存，单物体结果不能冒充完整地图。

根因、修复与本轮证据见[几何与来源记录](../archive/gpu-scene-geometry-20260923.md)。

## 复现

### 1B 独立 native Scene

更新 package 后运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_native.ps1 -OutputDirectory tmp/scene-native-check
```

脚本等待每个 GUI 进程结束，检查真实退出码、资源退休、零 bridge、两种 extent、增长和五种 present
故障，并运行 pose、逻辑、Campaign normal-native、显式加载渲染 fixture 的 normal-map-wall
和 normal-map-sign，以及 Campaign normal-model-legacy、normal-model-special、normal-model-infected、normal-actor-rifleman、normal-actor-standard、normal-actor-assault、normal-projectile、normal-pickup 回归。
这些镜头的 `SCENE-WORLD-GPU` 日志检查首次地图上传、次帧 cache hit 与 120 个角色 draw；定向镜头还检查
非零离屏覆盖。两个小队、投射物和拾取物镜头另保存 mixed BMP 与 Scene PPM，检查无遮挡角色、旗帜、投射物和模型拾取物内部采样点的 RGB 精确一致。正常呈现
仍走 mixed，审计额外提交的 Scene target 有显式诊断 readback。输出目录必须是新目录。提供
`-ValidationLayerDirectory <包含 VkLayer_khronos_validation.json 的目录>` 时，还要求日志证明 layer
实际加载及 Synchronization 开启；未提供时 manifest 明确记录 validation/sync 未运行，不能作为最终签收。

同步门禁与设备定向示例（layer 及其 SPIRV-Tools、MinGW runtime DLL 必须可加载）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_native.ps1 -OutputDirectory tmp/scene-sync-nvidia -ValidationLayerDirectory C:/path/to/layer/bin -DeviceVendor 10de
```

`-DeviceVendor` 设置进程级 `RF_GPU_VULKAN_VENDOR_ID`（十六进制），并校验实际 Scene adapter；
找不到指定 vendor 的可用 queue 时失败，不替换设备。backend 记录全部 Vulkan 设备及原始 driver/API 版本。
`-ValidationLayerDirectory` 可以使用相对仓库工作目录的路径；脚本在启动 package 进程前解析为绝对路径。
`PASS - available gates` 表示本次脚本覆盖的检查通过；阶段 1B 的其余退出条件以活动计划为准。
manifest 中保留的历史设备专项字段不参与当前主线签收。
脚本兼容 Khronos 新旧启用日志，但始终同时要求 loader 插入 layer 和明确的 Synchronization 启用证据；
manifest 保留 layer DLL/JSON 与 executable 哈希。只看到 manifest、环境变量或零错误文本均不算通过。
本轮主设备配置与验证证据见[同步与绕序记录](../archive/gpu-scene-sync-culling-20260924.md)。

直接入口为 `rasterfall.exe --gpu-scene-native-fixture --frames 120`，无需选择 normal renderer。
它固定使用地图 fixture 中 `opaque_box` 的冻结坐标及正式 mesh builder、一个 rifleman、一个 HEAD 附件；
camera 固定为 `(-3850,-420,1800)` 朝 +Z，actor 固定为 `(-3840,-900,3300)`、MOVE 160 ms。
不消费当前游戏地图或可交互 actor；普通启动完全不进入此路径。当前为 Windows 专项，其他平台明确失败。

首帧在 native 提交前运行 palette/generation/material 拒绝测试、body GPU vertex diff，以及 composite
与三件单独绘制的 color/depth 对照。capture 写入 package 下 `scene-native.ppm` 和三张 object PPM，
脚本复制到证据目录。等深度遵循实际 GREATER_OR_EQUAL 的后提交覆盖；三件必须可见且 body/map、body/head
存在交叠，完整 composite 必须逐像素选择相应最近深度与颜色。诊断有显式 readback；后续 native 帧不做
readback 或 CPU framebuffer copy，也没有 Raster bridge。

首帧还用独立的三层三角形验证透明管线：先绘制不透明蓝色底层，再按提交顺序叠加
半透明红色和绿色；中心像素检查 source-over 结果，透明绘制前后的深度必须相同。
这是 Scene 离屏管线验证，地图平台和 air gate 尚未提交到透明 WORLD pass。

首帧另有 `SCENE static-prop-clip=PASS`：同一大三角形分别验证恒深度全屏覆盖和穿越近裁剪面。
输入超过整数兼容管线的保守屏幕范围；该管线必须拒绝，Scene 硬件裁剪必须保留可见像素。
恒深度用例逐像素核对颜色和深度，近裁剪用例核对颜色、有效深度及中心覆盖。逻辑回归还拒绝
不安全的整数变换和蒙皮模型。正常 `actor-standard`、`actor-rifleman` 镜头的静态实例暂缓项必须为零，
次帧复用缓存；这些门槛由原生脚本检查，不代表完整 WORLD 画面合同已批准。

固定专项在第 21 帧加入不被 draw 引用的额外 skin vertices，验证 backing 增长；第 41/61 帧调整原生窗口
大小；第 81 帧在 submit 后 invalidate world 资源，检查 GPU 完成前仍有三份 pin、完成后才释放，再加载新
generation。需要至少 82 帧覆盖完整生命周期，脚本使用 120 帧。pose 仍每次独立求值，CPU upload/pack backing
在 slot 退休后按容量复用；脚本检查增长及 world 退休后的复用。`SCENE gpu-time` 按冻结 frame ID 记录
WORLD draw 和 present blit 的 GPU 毫秒数；脚本要求 119 个有效样本。此前独立提交的 upload/skinning 不在
这两个区间内。分段日志仅用于预算，不代表完整产品帧，也不运行正式性能 A/B。

生命周期入口在三件套帧结束后额外运行真实 Runtime Map 网格的离屏 GPU 检查。它加载
`gpu_scene_render_fixture.map`，冻结 11 条 world 值及地面范围/出生区，把六类不透明模型经独立 registry 的 pin 和
GPU resource cache 上传为 28 个 draw，再检查 WORLD color/depth 非空覆盖。重复 prepare 必须命中
cache；旧代失效时 pinned device resource 保留，帧退休后 cache collect 释放。
fixture 无 boundary wall、普通 `MODEL` 盒体或展示模型，因此十一类资源中的非空模型为六类，其中包含 SIGN；
Campaign 正常帧审计另冻结 object 值，并将 boundary wall 和普通 `MODEL` 盒体提交到离屏 Scene WORLD。
正常 Campaign 审计还将可见静态 RMESH 从冻结 object 值和资产 profile 编码到同一 WORLD；
日志分别报告资产数、RMESH draw、镜头外剔除和预检暂缓。fixture 的 28 draw 数值不含 RMESH。
`SCENE world-gpu=PASS` 包含 upload、hit、retirement 计数；此项有显式诊断 readback，
并不接入正常帧 Scene 呈现。

对单个固定镜头同时给出 `--frame-audit --gpu-frame-capture <output.bmp>
--gpu-capture-frame 1` 时，现行 mixed 画面写入 `<output.bmp>`，同帧离屏 Scene WORLD
读回写入 `<output.bmp>.scene.ppm`，供局部画面差分。`--gpu-normal-scene actor-rifleman 0`
将镜头置于真实 session rifleman 前方；Scene PPM 包含地图与该角色 body、三件被动装备及 AK 武器，
不含天空、其他角色、viewmodel 或 HUD。局部像素核对不能替代完整视觉合同。

本轮设备与结果见 [1B 现场记录](../archive/gpu-scene-native-20260923.md)。

### 冻结 actor 的 pose/附件数据回归

`tools/gpu_scene_play.ps1 -Stage Combat` 包含西侧走廊死亡和特效容量回归。
`--gpu-normal-scene enemy-death-west 0` 在老地图 x≈−44000 放置 16 个死亡 Charger，
运行 100 个固定 tick，覆盖碎片生成、身体渐隐和消失；旧错误会在碎片绝对坐标上传时退出。
`--gpu-normal-scene scene-effects-stress 0` 交替提交满池死亡碎片和单条长弹道，
覆盖资源分块、容量缩小后增长和超长世界三角形细分。两者都走真实 Scene native 提交，
须检查实际退出码和连续帧审计；更新 package 后执行，输出目录必须新建。

动态敌人身体专项使用 `tools/gpu_scene_enemies.ps1`。先完成 `windows/NativeCodex.ps1 package`，
再串行运行专项；package 会替换资产目录，不能与正在运行的游戏或 GPU 验证重叠。
`-ValidationLayerDirectory tmp/scene-validation-tools/mingw64/bin` 可启用已有本地 Khronos layer 与同步验证。
脚本检查 `enemy-special 0` 的首帧与动作推进，以及 `near 30` 的 AUTO、Block、Humanoid 存活身体与两帧动作推进、真实退出码、
native present 和同帧 mixed BMP / Scene PPM，并调用 `tools/gpu_scene_enemy_pixels.py` 检查固定局部像素。
像素检查器可单独接收已有输出目录重跑。`SCENE-ENEMY` 分别记录身体项、draw、暂缓与远距剔除。
三个特感和六种普通感染体的存活身体进入 WORLD；阴影、舌头、死亡和显式 LEGACY 尚未覆盖，不能对完整画面要求逐像素相等。
源码中的逻辑回归另检查三类刚性几何的确定性、pose 变化、失败传播和单帧冻结边界。
`--gpu-scene-pose-test` 另检查六种感染体资源、步态变化、冻结重放、旧 scratch pose 隔离与失败传播。
普通感染体仍逐帧提取并更新动态顶点，诊断包含同步读回，因此该专项不用于 whole-loop 性能或长时运行结论。

更新 package 后，从 package root 运行 `rasterfall.exe --gpu-scene-pose-test`，或使用
`powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --gpu-scene-pose-test`。
GUI executable 仍须按下方 `Start-Process -Wait -PassThru` 的方式等待，并检查真实退出码与日志。
此专项需要 package 中的 RF humanoid body、gear、AK 和动作资源；缺失时失败，不跳过。

测试复用真实 actor 构造器和 session roster，检查冻结值重复提取、MOVE 时间回卷、FIRE lower phase、
slot 移动、同 ID 替换、world 切换、extent 变化、session unload 后重放、空 actor 和事务失败。
body palette 的全部顶点位置/法线与 CPU instance skinning 对照；gear 与直接 socket 查询对照，
武器 raw mesh-to-world 与现有 authored placement 链对照。`--logic-test` 保留来源和地图对照回归。
该专项不提交 GPU，不证明 swapchain resize、共享深度、零 bridge 或 validation/sync 通过；这些仍是
独立 Scene target/submit/retire 接线后的门禁。不以此专项的耗时判断正式帧性能收益。

先用 `windows/NativeCodex.ps1 package` 更新 Windows 原生 package。`windows/Makefile` 将整个
`rasterfall/assets` 复制到 package，无需单独列举地图。以下命令从仓库根运行；capture
写入 `tmp/`，不提交生成物：

```powershell
$repo = (Resolve-Path .).Path
$package = Join-Path $repo 'build-windows/rasterfall-windows'
$map = Join-Path $package 'rasterfall/assets/maps/gpu_scene_render_fixture.map'
$evidence = Join-Path $repo ("tmp/gpu-scene-fixture-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$out = Join-Path $evidence 'map-ramp-f001.bmp'
$mapArg = '"' + $map + '"'
$outArg = '"' + $out + '"'
# Some development shells inject both PATH and Path; Start-Process needs one.
$pathValue = [Environment]::GetEnvironmentVariable('Path', 'Process')
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')
$arguments = @('--map', $mapArg, '--renderer', 'gpu-compute', '--gpu-required',
  '--gpu-native-present', '--gpu-normal-scene', 'map-ramp', '0',
  '--gpu-normal-fixed-tick', '--gpu-frame-capture', $outArg,
  '--gpu-capture-frame', '1', '--frame-audit', '--frames', '1')
$process = Start-Process -FilePath (Join-Path $package 'rasterfall.exe') `
  -WorkingDirectory $package -ArgumentList $arguments -WindowStyle Hidden `
  -RedirectStandardOutput (Join-Path $evidence 'gpu.stdout.log') `
  -RedirectStandardError (Join-Path $evidence 'gpu.stderr.log') `
  -Wait -PassThru
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $out)) {
  throw "GPU capture failed: exit=$($process.ExitCode)"
}
```

把 `map-ramp` 改成 `map-wall` 或 `map-platform` 可复用固定相机。固定相机分别位于
`(0,0)` 并朝向左墙、正前方、右平台；参数解析与实际姿态以当前 `--help` 和
`rf_game_runtime.c` 为准。记录地图、package executable 和 BMP 的 SHA256，以及运行日志中的
`Loading world source`、`FRAME-AUDIT path=gpu-native`、`map-draw`、`GPU-FRAME`
和 capture 保存行。capture 产生专用 readback 字节数；普通运行的零 readback 断言不能直接套用
capture 帧。CPU 对照可用相同地图、镜头、tick、资源与帧数运行
`--renderer cpu --gpu-normal-scene map-ramp 0 --gpu-normal-fixed-tick --dump-frame <path> --frames 1`；
当前 `--dump-frame` 实际写 PPM P6 格式，路径应使用 `.ppm`。GUI 进程仍使用上面的
`Start-Process -Wait -PassThru` 方式并检查退出码和输出文件。

首轮证据、输入哈希及未解决的画面差异见[2026-09-23 现场记录](../archive/gpu-scene-fixture-20260923.md)。

当前固定画面来自 mixed renderer；它可作为 GPU Scene 迁移前的输入和画面证据，不能证明
新的 GPU Scene 光栅器存在或正确。首份候选还须按[画面差异合同](../plans/gpu-scene-visual-contract.md)
冻结输入、记录差分和审阅逐项容差。`--normal-frame-audit` 内部会重载正式 Campaign，
不能用它的输出充当这张地图的对照图。
