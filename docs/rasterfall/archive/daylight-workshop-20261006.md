# 天空漫反射、车间照明与内部楼梯（2026-10-06）

> 状态：历史现场；稳定合同见 [GPU 光照](../architecture/gpu-lighting.md)、[车间指南](../guides/frontier-station-01.md)
> 范围：用户授权的自然光改进、真实灯具、车间外墙/楼梯翻新及壁灯射向修正

## 问题与实现

原背景天空不参与 WORLD 照明；旧环境填充仅用向上可见性判定，在檐下缩至 10%。
原 DDGI 只注入局部灯，且布局依赖固定灯位置，因此实验区背光墙和没有室内灯的车间几乎无反弹。

新增相机无关的程序天空方向缓存，按水平天空照度归一化，排除太阳盘；以独立建筑探针场
传输天空和太阳的最多两次漫反射。没有有效探针时使用真实建筑遮挡的直接天空回退，
有顶区域不接入无遮挡的环境亮度。固定灯探针场保持独立预算。默认替代旧艺术填充，
`RF_GPU_DAYLIGHT=0` 可完整恢复旧路径。GTAO 与镜面 IBL 未实现。

车间中央仅靠自然采光仍暗，经用户确认增加真实吸顶灯与浅色天花板。随后用户要求翻新建筑：
外侧两段大斜坡改为东北角内部封闭折返楼梯，主楼板按楼梯洞口分区，完整外墙与顶盖闭合；
一层、二层和屋顶通过有限厚度踏步/平台相连。灯具由地图生成器和既有模型 profile 提供，
楼梯白色灯面改为与出光轴一致的前下斜面，插口移到透光面外。

## 光度与性能证据

根目录证据：`tmp/daylight-20261006/`；改前基线在
`tmp/environment-lighting-investigation-20261006/`。测点是同一镜头的可见接收面矩形，
为屏幕样本平均值，不能代表整个房间或地面的平均照度。车间数值来自翻新几何前的布光对照，
后续楼层 bounds 和镜头中心已变化，不直接沿用这些屏幕坐标作最终地图对照。

| 同镜头测点 | 改前直接 / GI / 艺术填充 lux | 自然光及室内灯具后 lux |
| --- | --- | --- |
| 实验区塔楼背光墙 | 0 / 0 / 5.195 | 0 / 112.209 / 0 |
| 车间一层中央地面 | 0 / 0.015 / 7.642 | 393.318 / 59.014 / 0 |
| 车间二层中央地面 | 0 / 0 / 7.642 | 417.805 / 68.905 / 0 |

曝光前 HDR 读取分别见 `r1/`、`workshop-lamps-r1/`、`measured-lighting.json`；
正常画面另行捕获，未通过提高曝光或添加常量填充获得上述改进。默认天空 120 lux 和太阳约
274 lux 仍是美术标定，不是现实晴天条件；有界反弹和 RGB 光度也不是照明设计认证。

RTX 3050 Laptop，1920×1080，`atmosphere-lab`，同包自然光开关交替三轮，每轮 120 帧预热、
240 帧测量，关闭 capture/validation。三轮中位的主场景 GPU P50：2.827 → 4.387 ms，
GPU P95：2.955 → 4.511 ms；WORLD P50：1.047 → 2.496 ms，天空 compute P50：
0.202 → 0.265 ms，GI compute P50：0.192 → 0.230 ms。各分位数不相加；
主要新增开销在片元环境采样，证据为 `perf-atmosphere-r1/`。这不是全游戏稳定帧率签收。

最终车间俯视使用同样的分辨率、预热和采样长度，自然光开关交替三轮：
GPU P50 三轮中位 19.664 → 26.307 ms，GPU P95 21.166 → 27.746 ms；
主循环帧时间 P50 27.244 → 33.802 ms，见 `perf-workshop-final/`。
密集楼板附近的探针可见性缓存容量由每格 8 个盒提高到 32 个；只有完整覆盖该格的候选集合
才使用缓存，溢出或含三角形时仍执行建筑查询，不省略遮挡物。固定灯缓存覆盖 5237/6936 格，
自然光覆盖 7324/11071 格。优化前车间开启自然光的 GPU P50 三轮中位为 30.242 ms；
这两次构建的采样并非交替 A/B，仅供趋势参考。最终开关对照仍新增约 6.643 ms，
该镜头尚未达到稳定 60 FPS，不能以实验区低负载结果代替车间性能结论。

## 验证与边界

以下回退设备结果是已完成的历史证据。用户在收尾时明确要求后续集中维护当前 NVIDIA 独显
硬件主线，软件 BVH 与核显不再作为维护和验收门槛，当前优先级见活动计划。

- 17 个 SPIR-V 变体生成通过；Windows native build 通过。
- `lighting-hardware-r3/`、`lighting-software-validation-r1/`、`lighting-amd-r1/`：
  RTX 硬件、RTX 软件 BVH、AMD 核显回退通过光照专项回归。覆盖无灯天空、纯太阳反弹、封闭房间、
  远处屋顶、切顶、曝光/相机平移独立、resize、清空和无效替换；既有局部灯与光度回归也通过。
- 最终缓存和 shader 版本重跑 `lighting-final-hardware/`、`lighting-final-software-validation/`、
  `lighting-final-amd/`，均实际退出 0；前两者确认 Core/Synchronization validation 已启用且无错误。
  `lighting-final-software/` 的首次补测缺少 layer 运行时 PATH，功能通过但不能作为 validation 证据，
  补全 MinGW 运行时路径后由上述软件 validation 轮次覆盖。
- `native-validation-r1/`：实验区和车间两层真实 native capture，硬件查询、同步验证通过，
  无 VUID、SYNC-HAZARD 或 validation error。软件专项日志确认 Synchronization 已启用。
- `test-r6.log`：车间翻新后的完整逻辑回归退出 0，包含正常导航上二层、屋顶和返回出生点；
  `test-r5.log` 保留早期守军出生点受阻的失败，调整地图站位后解决。
- `tools/test_building_kit.py` 通过；地图生成后与源一致。Blender 验证壁灯两片发光三角形
  法线与指定前下方向一致，导入为 38 三角形 RMESH；吸顶灯资源未改变。
- `linux-targeted-build.log`：Linux hosted GPU backend、共享 Scene owner 与 prop 编译通过，
  hosted GPU 工具补齐数学库链接。扩大检查到完整 runtime 时，既有 freestanding lane 存在
  `getenv`、GUI 类型与 probe stats 声明缺失，见 `linux-build-r2.log`；本轮未修复该独立问题，
  也未以 Linux 编译代替 Windows 实机签收。

`workshop-native-stairs-r2/report.json` 为 `passed=true`、实际退出 0：正常 Win32 输入选择五名
队友、向二层下令，确认真实队员通过内部楼梯到二层，同时检查楼层切换、主视图剖切与单位聚焦。
首次普通窗口焦点获取失败的 `workshop-native-stairs-r1/` 保留为失败证据。
最终外墙、车间、两层楼梯入口、楼层、屋顶与前哨站灯具共九个近景在 `final-views-r1/`。
优化后 `final-verified/` 重拍实验区、车间二层和前哨站一层，全部通过 native 同步验证；
三幅画面与优化前逐像素完全一致，见 `final-pixel-comparison.json`。

自然光只使用静态建筑结构，未覆盖动态道具光追遮挡、镜面环境反射、两次以上反弹。
探针有限密度和六瓣表示会造成开口附近近似，天气变化受轮转/历史混合延迟影响。
本轮没有重跑完整十三守军肃清、设施回收、制造与返回任务闭环；通行回归和短场景检查不替代该验收。
