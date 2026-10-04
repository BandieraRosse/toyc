# 多视图渲染、动作求值与入图预热优化

> 状态：历史实施现场
> 日期：2026-10-04
> 范围：用户授权的通用性能优化，归玩家界面 V2 唯一活动计划

## 比较方法

基于 `53ba3fc` 开始，保留改前 Windows native 二进制，仅增加双镜头采样和真实 RTS 单位选择诊断。
改前/改后共用同一个 staged 资产目录，每轮反转 AB/BA 顺序及场景顺序，五轮、每项 120 帧稳态预热与
360 帧采样。使用 Outpost、真实时钟、120 FPS 上限、1280×720、immediate present、固定天空 time=0、
scale=4、默认展示关闭。required vendor 为 0x10de，设备为 RTX 3050 Laptop GPU。正式采样期间没有
并行构建或另一 GPU 任务；仓库只做小量文本检查。电源方案前后均为均衡模式，结束核对交流电在线；
电源记录保存在 `tmp/multiview/power.json`。

基线不是 experiment UI，也不是另一个历史资产包；正式玩家 UI 和两路真实镜头在两版中都启用。
正常帧原有的逐角色成本日志改为跟随 probe 的 quiet 状态，改前保留原日志行为，改后只在显式诊断输出。
这项减少热路径 IO 的收益包含在整体对比内，不单独归因给蒙皮或 GPU 绘制。

| 二进制 | SHA-256 |
| --- | --- |
| reference | `DCCC467F1A8AEBDB0361EAEA65E6CBBC0433A4AB3353F94012C39287EE203071` |
| optimized | `CBB3D17CFA70EF891BCE9025AAFF48E57B920E96487BAF249F2C2B44F04DB074` |

工具为 `tools/gpu_player_ui_perf.ps1`；原始 stdout/stderr、参数、资产 hash、退出码、分槽成本和预热记录在
`tmp/multiview/final-ab/r1-reference/` 至 `r5-optimized/`。汇总为 `tmp/multiview/final-ab/summary.json`。
这些生成物不提交版本控制；每项 `SCENE-PERF` 和 `UI-PERF` 的 valid 均为 1，真实 GPU 时间戳有效。

## 720p 五轮结果

下表取五个单轮指标的中位数，单位为 ms；P95 越低表示较慢帧的等待越短。

| 场景 | 帧时间 P50，改前 → 改后 | 帧时间 P95，改前 → 改后 | P95 降幅 | 改后五轮 P95 范围 |
| --- | ---: | ---: | ---: | ---: |
| FPS，无辅助镜头 | 11.06 → 8.54 | 12.36 → 9.47 | 23.4% | 9.27–10.47 |
| RTS，单位镜头 | 12.63 → 9.58 | 21.02 → 14.32 | 31.9% | 14.07–14.61 |
| FPS，剧情镜头 | 12.61 → 9.62 | 21.74 → 15.37 | 29.3% | 15.09–15.69 |
| RTS，双镜头 | 14.37 → 10.97 | 33.60 → 16.44 | 51.1% | 16.32–17.52 |

双镜头平均帧时间的五轮中位数为 17.89 → 12.25 ms；
P99 为 36.18 → 18.10 ms。
双镜头主帧 preparation P95 为 25.09 → 10.33 ms。
同轮主视图 draw/shadow draw 保持相同，双镜头示例为 1011/4055，FPS 为 819/4434；
未降低主分辨率、辅助分辨率、模型细节、阴影配置或默认镜头刷新率。

两路辅助刷新 CPU P50（包含同步 GPU 退休）分别从
9.22/8.65 ms 降为
5.34/4.68 ms。
五轮优化后采样的 `UI-VIEW together` 均为 0，改前两路刷新集中到同一主帧。
样本数随实际时间变化，不能把辅助成本或各阶段分位数相加。Windows 主线程 CPU 记账为粗粒度，
部分短帧会显示 0/15625 us，不据此声称 CPU 零开销。

## 1080p 与镜头状态补查

相同设置另做 1920×1080 的一轮新旧对照，实际 `extent` 和 valid 已核对。以下是补充观察，不能代替
该分辨率下的五轮正式结论；证据在 `tmp/multiview/1080p/`。

| 场景 | 帧时间 P50，改前 → 改后 / ms | 帧时间 P95，改前 → 改后 / ms |
| --- | ---: | ---: |
| FPS，无辅助镜头 | 11.26 → 8.63 | 12.60 → 9.50 |
| RTS，双镜头 | 14.35 → 11.27 | 31.20 → 16.09 |

`tmp/multiview/aux-states/` 覆盖武器预览、近处收起/关闭、远程通讯及远程收起，每项 180 个采样，valid=1；
收起/关闭的辅助新增帧均为 0，预览和远程现场仍有真实 GPU 更新。
双镜头另以 `capture.request` 做正常运行图检，`tmp/multiview/visual/frame-000003.scene.ppm`（帧 199）
确认主 RTS 场景、NULL 现场、选中单位现场与 HUD 共存，人物及背景正常。该显式读回运行不进入性能表。

## 实现与预热证据

- 主视图先完成相机无关资源、蒙皮和几何准备；WORLD 子视图复用主资源缓存、角色 draw、匹配冻结值
  的敌人/程序角色几何与显示 packet，保留各自镜头、阴影和渲染目标。
- Vulkan resource 支持多个同 device reader；只由 owner 更新，要求所有 reader 退休；关闭和销毁
  清理双向引用及绑定。保留原单槽同步语义，没有引入跨帧在途资源。
- roster 验证改为整批一次，复用每次重置的串行 pose scratch。双骨 IK 只更新改变骨骼的后代，
  持枪迭代仅在位置、方向和累计量完全不变时提前结束，原容差和上限保持。
- 两镜头按绝对周期错开半个周期；来源变化仍立即刷新。正常帧减少重复环境变量查询和成本日志输出。
- 每次地图代际首个 native 图像前执行正常离屏准备，初始化主要静态资源、当前冻结角色、显示几何、
  主目标及两路辅助目标；不推进动作历史，加载时间排除在固定步长累计器外。

五轮 Outpost 各场景的新进程记录共 20 次预热，墙钟 2.04–2.36 秒；
主准备上传量约 107.8 MB。该数字是上传流量，**不是显存占用**，也不包含所有
辅助目标/驱动分配。world-cycle 在帧 30/60/90 切换，代际 2/4/6/8 分别出现预热日志，旧 registry
在切换记录中 live/retired/pinned 均为 0。

静态地图和当前冻结来源的主要首次成本已经前移；未来新出现的敌人类型、武器预览、开启的展示及
动态容量增长仍可能首次加载。应用持有 device-local 分配，不保证驱动/操作系统永不迁移物理显存。
固定站位采样和本次切图证据不代表所有首次移动路线均无卡顿。

## 验证

- Windows doctor、native build 完成；当前 staged exe 已更新。
- `--gpu-scene-pose-test` 通过，新增整批/逐角色逐字节一致、冻结输入不变、downed 跳过、容量失败检查；
  既有骨骼、武器、附件、动作回放和 scratch 隔离检查通过。
- `--logic-test` 原生进程退出 0；`tmp/multiview/logic-final.out/.err` 保存原始结果，stderr 中包含
  逻辑回归主动构造的缺图和非法参数拒绝，不是正式地图加载失败。
- 完整 `gpu-graphics-test` 在 NVIDIA RTX 3050 与 AMD Radeon 集显分别通过 7138 checks，包含双消费者共享、reader 关闭/重建、
  owner 蒙皮更新、纹理、颜色/深度、双镜头更新/隐藏、HUD 顺序、零辅助读回和 bridge。
- retained-display contract test 通过内容失效、顺序、原点、bounds、增长/缩小、预算、失败和 cleanup。
- `tools/gpu_scene_play.ps1 -Stage All -Frames 160` 通过：真实输入/resize、120 帧 world-cycle、
  100 帧死亡清理、effects 容量、160 帧连续波次及五种 acquire/record/submit/present 故障链。
  证据在 `tmp/multiview/lifecycle/`。未安装可用 validation layer，本轮不宣称 Vulkan validation/sync layer 通过。
- 文档链接/UTF-8 检查及 `git diff --check` 通过；渲染/动画架构、任务路由和唯一活动计划同步更新。

## 边界

结果证明列明场景的性能改善。双镜头个别轮次 P95 仍超过 16.67 ms，不能据此签收所有场景稳定 60 FPS。
长期显存峰值/增长、全部地图冷路线、其他驱动及跨帧异步 pipeline 保留为未验证或后续工作。
稳定合同见[GPU 架构](../architecture/gpu-rendering-architecture.md)、[动画架构](../architecture/animation-architecture.md)；
复现入口见[性能诊断](../guides/rendering-performance.md)，当前范围见[活动计划](../plans/player-ui-v2.md)。
