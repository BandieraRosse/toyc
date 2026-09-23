# GPU Scene 固定渲染地图

> 状态：当前输入；GPU Scene renderer 尚未接入正常帧
>
> 事实入口：`rasterfall/assets/maps/gpu_scene_render_fixture.map`、`rasterfall/src/rasterfall_options.c`、`rasterfall/src/rf_game_runtime.c`
>
> 最近核对：2026-09-23

这张独立 V1 空间地图供固定画面和逐帧审计使用。`--map` 只覆盖本次进程；普通启动仍加载
`rasterfall.map`。地图没有 `attr.identity`，沿用 Campaign 的 World Content policy，因而
正常帧仍会出现 HUD、队友、viewmodel 等非地图内容。地图本身不声明这些玩法对象。

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
故障，并运行 pose、逻辑与旧 normal-native 回归。输出目录必须是新目录。提供
`-ValidationLayerDirectory <包含 VkLayer_khronos_validation.json 的目录>` 时，还要求日志证明 layer
实际加载及 Synchronization 开启；未提供时 manifest 明确记录 validation/sync 未运行，不能作为最终签收。

同步门禁与设备定向示例（layer 及其 SPIRV-Tools、MinGW runtime DLL 必须可加载）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_native.ps1 -OutputDirectory tmp/scene-sync-nvidia -ValidationLayerDirectory C:/path/to/layer/bin -DeviceVendor 10de
```

`-DeviceVendor` 设置进程级 `RF_GPU_VULKAN_VENDOR_ID`（十六进制），并校验实际 Scene adapter；
找不到指定 vendor 的可用 queue 时失败，不替换设备。backend 记录全部 Vulkan 设备及原始 driver/API 版本。
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

固定专项在第 21 帧加入不被 draw 引用的额外 skin vertices，验证 backing 增长；第 41/61 帧调整原生窗口
大小；第 81 帧在 submit 后 invalidate world 资源，检查 GPU 完成前仍有三份 pin、完成后才释放，再加载新
generation。需要至少 82 帧覆盖完整生命周期，脚本使用 120 帧。pose 仍每次独立求值，CPU upload/pack backing
尚未池化。分段微秒日志仅用于预算，不代表完整产品帧，也不运行正式性能 A/B。

本轮设备与结果见 [1B 现场记录](../archive/gpu-scene-native-20260923.md)。

### 冻结 actor 的 pose/附件数据回归

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
