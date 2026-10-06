# GPU 光照实验与验证

> 状态：当前操作指南
> 所有者：GPU Scene

## 建筑硬件光追与对照

默认自动探测设备能力，在支持 Vulkan 1.2 和 KHR Ray Query 时启用静态建筑硬件遮挡。
`RF_GPU_ARCHITECTURE` 可设为 `auto`、`hardware` 或 `software`；进程初始化时读取，切换需重启。
`hardware` 在不支持的设备上明确失败。日志须出现 `rf-gpu-ray: requested=... selected=...`，
硬件路径还会打印启用特性及建筑构建的三角形数、耗时、保留字节数。

```powershell
$env:RF_GPU_ARCHITECTURE='hardware'
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --gpu-lighting-test
python tools/outpost_lighting_check.py --output tmp/outpost-ray-hardware
$env:RF_GPU_ARCHITECTURE='software'
python tools/outpost_lighting_check.py --output tmp/outpost-ray-software
Remove-Item Env:RF_GPU_ARCHITECTURE
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareArchitecture -Views outpost-light-1f -Rounds 3 -Samples 240 -OutputDirectory tmp/outpost-ray-perf
```

捕获脚本当前固定 NVIDIA；跨厂商回退验证使用原生 `--gpu-lighting-test` 并指定 vendor。
性能脚本交替软件/硬件顺序，要求实际选中的后端匹配请求，保存同一 EXE/map 哈希。
性能采样关闭 validation，不开截图；同步与生命周期验证另行开启 validation layer，不能混算耗时。
光照回归包含空结构、替换、有限射线、双面、resize 后遮挡保留和无效输入不破坏旧结构。
硬件/软件对照验证建筑加速；间接光另按下述 DDGI 用例与同包开关检查，不据短帧诊断宣称全场景稳定帧率。

建筑查询已覆盖的 draw 默认不再重复绘制到传统阴影图。对照重复投影的成本：

```powershell
$env:RF_GPU_ARCHITECTURE='hardware'
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareArchitectureShadows -Views outpost-light-1f -Rounds 3 -Samples 240 -OutputDirectory tmp/outpost-shadow-perf
```

此轴交替设置 `RF_GPU_ARCHITECTURE_SHADOW_MAPS=1/0`，两种模式使用同一建筑查询后端。
截图对照可手动设置该变量后分别运行捕获脚本；除建筑阴影边缘的重复 PCF 暗边消失外，
须检查墙板、楼板、楼梯和门洞遮挡，以及道具/角色投影。`ARCHITECTURE SHADOW PASS`
保护清空后的回退、未标记物体投影、resize/失败替换和离屏计时区间。
`SCENE-PERF` 的 `shadow_p50_us/p95_us` 包含阴影绘制与深度复制，
`main_p50_us/p95_us` 包含主场景着色与合成；它们是各自分位数，不与天空分位数相加推导整帧。
限定设备的五轮结果见[建筑重复阴影剔除现场](../archive/architecture-shadow-dedup-20261005.md)。

## 分块灯表与光照成本定位

正常帧默认按 16×16 屏幕块筛选局部灯；手动 `RF_GPU_LIGHT_TILES=0/1` 可做捕获对照。
性能脚本提供同包交替开关、独立计数和逐项消融：

```powershell
$env:RF_GPU_ARCHITECTURE='hardware'
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareLightTiles -Views outpost-light-1f -Rounds 5 -Samples 240 -OutputDirectory tmp/light-tiles-perf
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareLightTiles -ProfileLights -Views outpost-light-1f -Rounds 1 -Samples 120 -OutputDirectory tmp/light-tiles-counts
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -LightAblations -Views outpost-light-1f -Rounds 3 -Samples 240 -OutputDirectory tmp/light-ablations
```

比较轴互斥；计数与消融不能混用。脚本在 `config.json` 保存模式和 EXE/map 哈希，
原始日志与 `report.json` 保留每次结果；切换任何模式都重启进程，按 Windows native 规则等待真实退出。
普通采样自动关闭计数和 validation，不开启 capture；带计数的运行显式标为 `valid=0`、`diagnostic=1`。

`SCENE-GPU-STAGES` 增加 tiles、world、transparent、viewmodel、post、hud、gi 的 P50/P95，单位微秒。
world 含天空合成，transparent 含特效，post 含视频合成；`diagnostic` 表示片元计数已开启，
`light_ablation` 单独记录消融掩码。各区间分位数不能相加推导整帧分位数。
`SCENE-LIGHT-PROFILE` 记录逐帧均值：shaded 为通过材质早退后的着色调用，candidates 为候选灯循环次数，
roof 为顶部缓存后的回退射线调用，sun/local 为可见性函数逻辑调用，visible 为局部光通过遮挡后的次数，pcf 为完整过滤核次数。
计数包含 overdraw，也可能因原子操作改变早期深度行为；不能当作普通运行的可见像素或实际硬件射线数。

`RF_GPU_LIGHT_ABLATION` 只用于诊断：`none` 为正常路径；`roof`、`sun`、`local` 分别跳过对应建筑遮挡；
`rays` 同时跳过三者；`pcf` 跳过阴影过滤；`brdf` 用简单漫反射替代原 BRDF。
遮挡消融会使原先被挡住的光继续执行 BRDF/PCF，时间可能反而增加；不得据差值宣称某项真实独占成本，
也不能把这些模式作为视觉或正式性能签收。普通运行应清除变量或设为 `none`。

`LIGHT TILES PASS` 回归按开关逐像素比较颜色和深度，覆盖空灯表、第 31/64/159 位、宽窄 spot、
近相机光源、混合视角和非整块 resize；建筑遮挡及原有光照回归仍共同运行。
限定设备的像素对照、五轮开关采样及三轮消融见[分块灯表现场](../archive/light-tiles-20261005.md)。

## 顶部缓存与 DDGI 原型

默认同时启用 `RF_GPU_ROOF_CACHE=1` 与 `RF_GPU_GI=1`，进程启动时读取，日志打印
`rf-gpu-indirect: roof-cache=... probe-gi=...`。GI 布置另外打印完整固定灯数、探针数、间距与缓冲大小。
先完成 build/stage，再启动捕获或采样；不要在 GUI 进程仍运行时执行会重建 package 的 `test/run`。

```powershell
$env:RF_GPU_ARCHITECTURE='hardware'
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareRoofCache -Views outpost-light-1f -Rounds 5 -Samples 240 -OutputDirectory tmp/roof-cache-perf
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareGI -Views outpost-light-1f -Rounds 5 -Samples 240 -OutputDirectory tmp/ddgi-cost
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareGIBounces -Views outpost-light-1f -Rounds 5 -Samples 240 -OutputDirectory tmp/ddgi-bounces
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareIndirect -Views outpost-light-1f -Rounds 5 -Samples 240 -OutputDirectory tmp/indirect-total
```

各比较轴互斥：顶部缓存轴固定 GI 关闭；GI 轴固定顶部缓存开启；组合轴从两者关闭切换到两者开启。
反弹轴固定顶部缓存和 GI 开启，交替 `RF_GPU_GI_BOUNCES=1/2`，验证默认第二次反弹的成本。
`gi_p50_us/p95_us` 仅包含探针 compute 更新，片元插值成本仍在 world/viewmodel 等绘制区间内。
首次完整探针预热与后续有界更新分开，不把启动成本藏入稳态结论；固定灯变化及动态灯影响范围等限制见
[DDGI 架构](../architecture/gpu-lighting.md#ddgi-漫反射原型)。

截图可分别设置 `RF_GPU_GI=0/1` 后运行 `outpost_lighting_check.py`，重点看 B1/一层/二层天花板、
上部墙面、门洞和楼板边缘。只比较顶部缓存时关闭 GI，要求颜色与深度保持一致。
`--gpu-lighting-test` 的 `INDIRECT PASS` 覆盖顶部缓存精确对照、纯反弹、黑反射率、封闭楼板、
静态来源去重、动态灯注入和历史衰减、隔离视图关闭、resize、拒绝无效更新及清空光场；
硬件、软件 BVH 和未支持 Ray Query 的设备均须检查。正常帧的原生截图与同步验证独立进行。
墙角用例把同一封闭腔体相对探针格移动，检查靠侧墙和顶面的接收面仍获得反弹；同时保留封闭隔板零漏光检查。
同一腔体对照一次/二次反弹，要求墙角进一步受光且深度不变；黑反射率、固定灯去重及历史衰减都覆盖默认两次反弹。
`python tools/test_building_kit.py` 保护嵌灯跨板边界、四向安装、凹槽无重叠、封闭背板和碰撞不变；
逻辑回归的 `persistent closed slab` 核对 Scene 楼板上下表面完整面积。实机再观察近处和远处灯面，
避免只看正视图遗漏模型共面暗纹。
`BOX-FINISH` 逻辑检查保护底面颜色经 Parser/Runtime/投影后的缺省、显式黑色与独立地板色；
`BOX REFLECTANCE PASS` 在真实 GPU 上检查白底面产生反弹、显式黑底面保持黑以及其他面颜色不串入底面。
调整室内亮度先检查表面反射率和灯具 profile，再核对曝光；同时检查天花板、上部墙面、受光地面和
家具细节，不能仅用灯面已经发白来推断照度。截图属于视觉校准，不等于真实 lux 测量。
限定镜头结果与原型限制见[顶部缓存与 DDGI 现场](../archive/ddgi-prototype-20261006.md)。
后续嵌灯、墙角和两次反弹的原生证据见[本轮优化现场](../archive/ddgi-corners-ceiling-20261006.md)。
室内材质、配光与亮度对照见[室内亮度校准现场](../archive/indoor-lighting-calibration-20261006.md)。

## 天空、太阳反弹与车间自然光

正常 Scene 默认启用 `RF_GPU_DAYLIGHT=1`。该路径从程序天空采样漫反射环境并由独立建筑探针
传输天空和太阳反弹，替代旧艺术填充；`0` 恢复旧画面以便对照。它不等于镜面 IBL 或 GTAO。
`RF_GPU_GI=0` 关闭两种探针场，但仍以建筑射线检查直接天空；`disable_indirect` 的固定光照实验
维持旧合同。详细范围见[自然光架构](../architecture/gpu-lighting.md#天空漫反射与太阳反弹)。

```powershell
python tools/gpu_daylight_check.py --output tmp/daylight-capture --meter
python tools/gpu_daylight_check.py --output tmp/daylight-legacy --daylight 0
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareDaylight -Views atmosphere-lab -Rounds 3 -Samples 240 -OutputDirectory tmp/daylight-perf
```

捕获脚本使用源地图、当前暂存 exe，固定天空时间和逻辑步，保存哈希、native 审计与 PNG；
`--meter` 另行读取曝光前的直接/GI/艺术填充通道，不能用其运行耗时代表普通帧性能。
性能轴交替关闭/开启自然光；frontier 镜头自动选正式站点地图。`-CompareRoofCache` 固定自然光关闭，
确保实际执行旧顶部可见性路径。天空时间段包含环境生成，GI 时间段包含灯具和自然光两场更新，
探针插值的片元成本计入 WORLD；不得把不同区间的分位数直接相加。

`DAYLIGHT PASS` 覆盖无灯开敞天空、入室天空、纯太阳反弹、封闭房间、超过反弹半径的远处遮挡、
曝光/相机平移不改变测量、切顶、resize、无效替换、清空和隔离实验。当前维护门槛为 NVIDIA 硬件路径；
软件与核显按用户决策不再要求复测。
正常图检同时看建筑背光面、门窗和室内深处；车间内真实吸顶灯、楼梯壁灯由地图生成器维护。

## 保持画面的光照性能对照

默认开启同位置 GI 探针的几何权重复用、灯表深度分段和不透明 WORLD 深度预通道。
实现合同见[光照架构](../architecture/gpu-lighting.md)与[深度提交](../architecture/gpu-rendering-architecture.md#scene-资源与同步)。
三项各有同包关闭/开启轴，不能同时指定：

```powershell
$env:RF_GPU_ARCHITECTURE='hardware'
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareGIReuse -Views frontier-floor-2 -Rounds 5 -Samples 240 -OutputDirectory tmp/gi-reuse-perf
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareLightDepth -Views outpost-light-1f -Rounds 5 -Samples 240 -OutputDirectory tmp/light-depth-perf
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareDepthPrepass -Views frontier-floor-2 -Rounds 5 -Samples 240 -OutputDirectory tmp/depth-prepass-perf
```

普通性能采样关闭计数、validation 和捕获；记录 GPU 温度、频率及限频状态，交替运行方向，
不能把不同温度或电源状态下的独立消融当作可相加的开销。先核对同镜头画面，再看 WORLD、
总 GPU 和端到端帧时间；更多 draw 不必然意味着更多 GPU 时间。
`GI REUSE PASS` 比较曝光前 HDR，覆盖同格、部分覆盖和独立加粗；`LIGHT TILES PASS` 比较
完整灯表、二维和深度分段，覆盖段边界、远距离、小尺寸、混合相机及高灯索引；
`DEPTH PREPASS PASS` 比较开关前后的颜色和深度。以上均在 `--gpu-lighting-test` 中运行。
限定设备的图检、同步与五轮对照见[光照性能优化现场](../archive/lighting-performance-20261006.md)。

## 低成本间接光候选对照

默认使用 fast，普通 GPU 游玩无需设置模式变量。移除已有覆盖后启动，CPU renderer 不消费此配置：

```powershell
$env:RF_GPU_ARCHITECTURE='hardware'
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
Remove-Item Env:RF_GPU_INDIRECT_MODE -ErrorAction SilentlyContinue
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --skip-boot --renderer gpu-scene
```

设置 `RF_GPU_INDIRECT_MODE=reference` 切换原照明对照路径；设为 `fast` 或移除变量恢复默认。
`direct_only_diag` 仅用于关闭整个间接
漫反射链的消融，不是可发布方案。模式在启动时读取；灯光配置、曝光、材质、直接阴影和
分辨率不随模式改变。fast 的空间表示、未覆盖回退和失效边界见[接收空间缓存](../architecture/gpu-lighting.md#可选接收空间缓存)。

先 build，确认没有其他 GUI/性能任务，再分别采样两张地图：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareIndirectMode -Views frontier-floor-2 -Rounds 5 -Samples 240 -OutputDirectory tmp/fast-workshop-perf
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareIndirectMode -Views outpost-light-1f -Rounds 5 -Samples 240 -OutputDirectory tmp/fast-outpost-perf
python tools/gpu_daylight_check.py --indirect-mode reference --views frontier-floor-2 frontier-workshop outpost-light-1f frontier-stairs frontier-stairs-upper --output tmp/fast-reference-images
python tools/gpu_daylight_check.py --indirect-mode fast --views frontier-floor-2 frontier-workshop outpost-light-1f frontier-stairs frontier-stairs-upper --output tmp/fast-candidate-images
```

模式轴与其他比较轴互斥，轮换 reference/fast/direct_only_diag 顺序，核对激活日志，
保存 EXE、SPIR-V、HEAD、地图哈希及 NVIDIA 温度/时钟/限频采样。截图默认 1280×720，
可用 `--width 1920 --height 1080` 对照性能分辨率；
性能默认 1920×1080、120 帧预热，不开启 capture、计数或 validation。运行过程中不构建。
`SCENE-GPU-STAGES` 的 `receiver` 是缓存 pass，`gi` 是探针 pass，各提供 P50/P95/P99；
总 GPU 和 WORLD 也提供 P99。不同阶段分位数不能相加；模式差值只是配对消融的边际成本。
首次进入、换图和灯状态变化的峰值要另跑 frame audit，不能用稳态样本隐藏初始化成本。

`--gpu-lighting-test` 的 `RECEIVER CACHE PASS` 检查真实 HDR 中的补光、封闭零照度、薄隔墙、
保留几何的固定灯改色/关灯、直接光消融、切顶、resize、拒绝无效替换、清空和隔离。
旧探针/环境合同显式使用 reference，接收缓存专项显式选择 fast/direct_only_diag，测试不随默认模式改变。
受控用例响应不代表全地图灯光变化；运动、人物过门洞和楼梯仍需独立实机检查。

## 原生运动与固定灯变化

先 build/stage，再分别运行两种模式的原生运动；不要与其他 GPU 或构建任务并发：

```powershell
python tools/gpu_indirect_motion.py --indirect-mode fast --output tmp/indirect-motion-fast
python tools/gpu_indirect_motion.py --indirect-mode reference --output tmp/indirect-motion-reference
python tools/gpu_indirect_motion.py --fixture-only --view outpost-light-1f --indirect-mode fast --output tmp/fixture-outpost-fast
python tools/gpu_indirect_motion.py --fixture-only --view frontier-floor-2 --indirect-mode fast --output tmp/fixture-workshop-fast
```

运动只通过现有 Win32/SDL 键鼠链路控制玩家，要求实际取得窗口焦点，保存玩家坐标、地面高度、
捕获帧号和 PNG；脚本拒绝仅有输入或坐标变化、却没有明显画面移动的结果。检查楼梯往返、
门洞和自由转向时的接收面、视角武器和明暗变化，FPS/RTS 切换独立记录。Windows 前台限制
必须先解决，不能用隐藏窗口收到键消息替代真实鼠标旋转。

`RF_GPU_FIXTURE_SEQUENCE=N` 是显式正常地图诊断：每 N 个冻结帧依次固定灯原色、红色、关闭、
恢复，再循环。它只改 presentation 的直接/间接固定光源，经正式 API 安装；不改 Game、碰撞、
太阳、天空、曝光和灯具自发光表面。首次安装、换图或阶段变化才更新固定光源，AUX 各自观察阶段。
`--fixture-only` 使用该轴保存四个实际呈现批次，核对接收面改色、关闭和恢复；reference 也应单独运行。
查看 `SCENE-FIXTURE-SEQUENCE`、建筑/探针初始化日志和生成的 `report.json`，区分状态切换峰值与稳态。

两项都含显式诊断读回，不能用于 FPS 或普通帧性能结论。性能采样必须清除
`RF_GPU_FIXTURE_SEQUENCE`、`RF_UI_CAPTURE_DIRECTORY`、HDR/计数及 validation，再跑正常同包对照。
换图使用 `tools/gpu_scene_play.ps1 -Stage World`，按 Windows Native 的进程退出与日志规则验收。

## 物理单位检查与照度读取

单位合同见[光照架构](../architecture/gpu-lighting.md#光度单位与显示合同)。固定灯用流明创作，
配光转换为峰值 cd，太阳输入 lux，自发光输入 cd/m²。曝光只是显示倍率。旧相对强度不能
直接加上单位标签；当前前哨站参数是迁移初值，真实房间的照度需要另行检查。

`--gpu-lighting-test` 的 `PHOTOMETRY PASS` 使用真实 RGBA16F 读回验证：

- 1000 lm 点光与两种锥角积分后仍为 1000 lm。
- 100 cd 光源在 1、2、4 m 的正入射照度为 100、25、6.25 lux；2 m、60° 入射为 12.5 lux。
- 改颜色、接收面反射率与显示曝光不改变指定照度；半径中点保留明确的范围衰减。
- 100 lux 太阳、0/100/10000 cd/m² 自发光，以及白色 Lambert 的 `100/π cd/m²` 输出。

在已暂存的 Windows native 包上读取可见表面的照度：

```powershell
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
python tools/gpu_light_meter.py --view outpost-light-1f --output tmp/light-meter
```

默认测中心单像素；`--region X0 Y0 X1 Y1` 指定 1280×720 捕获中的接收面矩形。
工具保存曝光前的 `meter.hdr`、运行日志和 `report.json`；报告分开给出直接光、GI、艺术填充，
`total` 只含直接光加 GI，`with_artistic_fill` 另加艺术填充。区域统计按屏幕像素加权，
不能冒充工作平面面积平均照度或照明规范合格报告。选择同一平面并避开边缘、天空、HUD、
viewmodel 和透明物；需要正式室内均匀度目标时，应增设明确高度和面积采样的工作平面。
照度诊断画面不是正常颜色截图，也不用于性能采样。

墙角可见性对照：默认 `RF_GPU_GI_VISIBILITY=1` 使用局部实体缓存；`2` 强制完整建筑查询，
应保持画面；`0` 恢复旧距离矩以复现误差。`RF_GPU_GI_DEBUG=1/2/3` 分别是完整查询、权重、
照度诊断，正常画面设 `0`。前哨站五镜头截图和性能必须分开：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareGIVisibility -Views outpost-light-1f -Rounds 3 -Samples 240
```

该比较交替完整查询和局部缓存，关闭诊断显示、HDR 捕获与 validation。两种模式都采用修正后的
几何可见性，不能把它们的时间差称为相对于旧距离矩版本的净收益。160 灯表回归覆盖第 31、64、
159 位及混合相机，固定灯不再随观察位置被截断。当前实现与限定实机证据见
[墙角与物理定标现场](../archive/photometric-lighting-20261006.md)。

## 实验区

实现边界见[GPU 实时光照](../architecture/gpu-lighting.md)。先运行 Windows native build，再启动：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 build
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_lighting_lab.ps1
```

实验区位于前哨站模型实验场东侧，可从模型场地步行进入。脚本直接选择前哨站并把相机放在实验区入口；也可使用 `--renderer gpu-scene --map rasterfall/assets/maps/outpost.map --gpu-normal-scene lighting-lab 0`。

普通进入默认关闭，东北侧独立终端按 E 开启/关闭；显式 `lighting-lab` 镜头预先开启。展示开关与时钟遵守[实验区合同](../reference/experiment-labs.md)。

三排球体从近到远分别为 PBR 非金属、金属、风格化非金属。每排从左到右粗糙度为 0.90、0.64、0.38、0.12，底座标签数字表示百分数。两盏暖色/冷色聚光灯沿轨道移动，球体和侧边立柱提供投影遮挡。球体是 GPU 展示资源，不参与玩法碰撞；地面通过正常地图 surface/collision 声明提供行走空间。CPU 只显示地图场地。

大厅渲染终端可开启 GPU 手电筒、切换角色风格化材质与纹理过滤。实时阴影和 PBR 是 GPU 默认能力；CPU 不支持这些高级选项。

## 自动与实机检查

前哨站三层固定视图：先用 `NativeCodex.ps1 test` 或 `run` 完成构建和资源暂存，再运行
`python tools/outpost_lighting_check.py --output tmp/outpost-lighting-review`。
该工具逐个等待真实子进程，检查退出码、日志及 GPU 捕获，并保存 B1、一层、二层、楼梯和研究翼 PNG。
固定镜头通过 `--gpu-normal-scene outpost-light-b1|outpost-light-1f|outpost-light-2f|outpost-light-stairs|outpost-light-research 0`
进入，地图需选择 `rasterfall/assets/maps/outpost.map`。截图不作为性能结论；成本使用
`tools/gpu_outpost_perf.ps1 -Views outpost-light-1f -Rounds 3 -Samples 240` 单独采样。

两件原创灯具通过 `python tools/outpost_lights_assets.py` 沿 Blender → GLB → 原生导入器重建；
机器需安装脚本所配置的 Blender 路径，原生 asset-tools 已构建。源模型不提交，运行 RMESH、manifest
和生成器共同维护。重建布局用 `python tools/outpost_storeys.py --write`。
检查灯具的发光面、锥体方向、门洞通光、楼板隔光和转向时的连续性；RTS 剖切仍保留完整建筑遮挡。

`rasterfall.exe --gpu-lighting-test` 在物理 Vulkan GPU 上做离屏行为回归，检查太阳/聚光灯遮挡、移动点光源、粗糙度、风格化响应、旧烘焙乘数无效和 resize。它有显式诊断 readback，不能代替 native present。

`ARCHITECTURE LIGHT PASS` 另外检查 point、超过两个动态阴影名额的 spot、太阳、有限光线段
和清空旧世界遮挡；BOX 与三角形对照保护实体解析求交及共边遮挡。静态 BVH 只包含实际建筑几何，
不能据此声称所有动态物体均已投射局部灯阴影。限定原生证据见[本轮现场](../archive/outpost-lighting-20261005.md)。

同一入口的 `LIGHTING RANGE PASS` 检查无太阳/局部灯时，仅改变法线朝向就能区分天空与地面填充，
并验证自发光 HDR 4/8 经真实 half-float 目标与色调映射后仍有亮度顺序，未同时裁成纯白。
这些是稳定行为合同，不以指定截图颜色或亮度常量作为美术签收。阴影新增连续 PCF 与级联过渡后，
仍须现场观察斜面自阴影、接触处悬浮、转头与跨级别时的边缘变化；使用
[正常场景性能采样](rendering-performance.md#实验园区正常场景采样)核对 GPU 分位数和实际阴影绘制量。

正常实验区截图可给启动参数增加 `--frames 3 --gpu-frame-capture <绝对路径> --gpu-capture-frame 2`。按 [Windows Native](windows-native.md) 等待进程句柄退出并检查日志和生成的 `.scene.ppm`。固定 capture 时使用固定展示时钟；观看灯移动时不要启用 capture。

资源生命周期继续使用 `tools/gpu_scene_play.ps1 -Stage Interactive` 和 `-Stage World`。修改共享 CPU/Scene 展示来源后运行 `--logic-test`，确认 CPU 烘焙仍可用。开启 Vulkan validation 时必须检查 VUID 和同步错误；有 validation 的短帧时间不是正式性能数据。
