# RB-0 测量阶段记录（历史）

> 归档：2026-09-22
> 本文保留签收前的现场、数字与当时阻塞条件，不定义当前推进状态。
> 当前状态见 [Raster / Bridge 计划](../gpu-raster-bridge-plan.md)。

最新状态以 [RB-0 专项续接](gpu-rb0-special-20260922.md) 为准；
修复前失败保留在 [RB-0 修复与续接](gpu-rb0-repair-20260922.md)。应先补齐画面及设备签收缺口，
再讨论 RB-1。下方阶段数字属于修复前记录。

最新 [RB-0 排查报告](gpu-rb0-investigation-20260922.md) 修正了下文阶段记录的解释：
producer 切换触发 flush 后，actor 裁剪仍使用旧命令起点，可能恢复已消费的槽位；GPU query pool
存在静默截断；graphics fence 不是 bridge 专属计时。当前先修复这些正确性/测量问题，
再重做固定 workload 基线。不得由跨轮 hash 一致推断观察代码没有改变实际负载。
本机新统计的平衡方案五轮已完成，结果和范围限制见报告；仍不进入 RB-1。

RB-0 不改变正常画面输出。先修正测量边界，防止把 workload 漂移、日志扰动或等待嵌套误判成 renderer
成本。

实施项：

- 为 near 0/30/60 和正式 Campaign 性能运行提供固定 simulation tick；性能比较必须使用同一初始状态、
  帧数、相机、extent 和资产 package。
- 按 producer 输出 RasterCmd 数量、span 数、主要命令类型、透明/不透明属性和可用时的像素覆盖估计。
  至少区分 world/map、enemy body、enemy rigid special、gear、weapon、transparent、effects、viewmodel、
  overlay 和 bridge。
- 将 `present_wall_ms` 内的 frame-slot wait、render fence/GPU completion、acquire、submit、present API 和
  presenter completion 分开记录。继续明确哪些字段互相嵌套，不允许在汇总中直接相加。
- 对 bridge 记录发生层边界、方向、次数、color/depth 字节和对应前后 producer。
- 扩展 `tools/gpu_metrics.ps1`，报告 median、P95、P99、maximum、最慢帧分类及关键计数；保留 GPU
  timestamp 自身 frame ID 的预热窗口语义。
- 同一 package 至少运行五轮；逐帧 audit 与无 audit 各有一组，记录电源状态、adapter、driver、extent
  和 package commit。

当前进展：`--gpu-normal-fixed-tick` 已同时覆盖 normal scene 与 Campaign wave repro；Full 的 near 0/30/60
和 Campaign 性能运行均使用一帧一 tick。mixed span 已携带 world/map、enemy body、enemy rigid special、
gear、weapon、transparent、effects、viewmodel、overlay 诊断身份；producer 切换只冻结已有 RasterCmd 边界，
不执行 GPU 工作或改变提交顺序。逐帧 bridge 审计输出 import/export、color/depth traffic、layer、前后
producer 与 target generation。`gpu_metrics.ps1` 汇总每类 producer 的 RasterCmd、span、opaque/transparent、
bridge event/bytes 的 minimum/maximum/distinct、whole-loop 相关系数和最慢 5% 帧明细，并生成排除计时与
target generation 的逐帧规范化 workload 和 SHA-256。当前 Intel package
的审计/无审计跨轮五次采样已完成；受权限限制的电源/驱动清单和另一物理 GPU 复核仍属于后续 RB-0
工作，完成前不得进入 RB-1。

五轮固定 workload 采样使用 `tools/gpu_rb0_sampling.ps1`。它在同一 package 上严格串行运行 near 0/30/60
各 120 帧与 Campaign 320 帧；默认审计组逐轮调用 schema 4 metrics 门禁，`-NoAudit` 对照组从最终 stats
提取不受逐帧日志扰动的 frame/P95/P99。输出目录保存 package 时间戳、commit、工作区、可用的
电源/adapter/driver 信息、原始日志、逐轮 metrics 和跨轮摘要。采样目录属于本地证据，不提交。

2026-09-22 已在当前 Windows Intel package 上完成审计组与 `-NoAudit` 组各五轮。near 0/30/60 的
bridge traffic 在审计组内分别固定为 12 次、88,473,600 bytes；Campaign 固定落在 14/16 次与
103,219,200/117,964,800 bytes 两种状态。逐帧日志会显著扰动重负载调度，因此 producer/bridge 审计组
只用于归因，无审计 stats 组用于性能对照，不能混为同一基线。schema 4 离线重算证明四个场景各自的
五轮逐帧规范化 workload 完全一致，Campaign 的两种 bridge 状态由确定帧序列触发。新增
`--gpu-rb0-stats` 在 16 帧预热后低扰动地保留整帧与关键分类数字，退出时统一输出分位数与最慢 5%，
采样脚本的 `-NoAudit` 组读取该结果。本机 CIM 电源与 driver 查询因权限拒绝，
且另一物理 GPU 尚未完成同组复核；这些仍是 RB-0 退出条件缺口，不进入 RB-1。

最新 package 的 `--gpu-rb0-stats` 格式门禁与正式五轮低扰动采样随后通过。预热后 whole-loop 的
五轮范围如下；“中位轮”是按各轮 median 排序后的中间一轮，不使用 maximum 或启动首帧：

| 场景 | median 范围 / 中位轮 | P95 范围 | P99 范围 |
| --- | ---: | ---: | ---: |
| near 0 | 24.660--28.735 / 25.262 ms | 28.392--35.876 ms | 30.002--43.307 ms |
| near 30 | 100.004--110.014 / 106.324 ms | 119.593--153.087 ms | 122.428--160.053 ms |
| near 60 | 146.750--203.240 / 157.118 ms | 174.307--583.242 ms | 178.815--641.747 ms |
| Campaign | 27.869--31.301 / 30.804 ms | 37.041--42.257 ms | 40.302--48.995 ms |

最慢 5% 中，near 0 的 30 帧均为 `external_scheduler`；near 30 为 17 帧
`external_scheduler`、13 帧 `slot_or_fence_wait`；near 60 为 12 帧 `external_scheduler`、18 帧
`slot_or_fence_wait`；Campaign 为 76 帧 `external_scheduler`、4 帧 `cpu_producer`。没有慢帧归为
`raster_workload` 或 `present_or_acquire`。near 60 的 P95/P99 呈明显跨轮双态，但慢帧中的 bridge
仍固定为 12 次、88,473,600 bytes；这说明长尾不是 workload 或 bridge 计数漂移，仍需在 RB-0 内继续
区分 GPU completion/slot wait 与未覆盖的外部调度。慢帧 `gpu_raster_us` 与 CPU wall Raster 都随
near 0→30→60 增长，方向一致，但二者的作用域不同且 CPU wall 字段互相嵌套，不得相加或解释为互斥
时间分解。本轮 manifest 记录活动电源方案为“节能”；CIM adapter/driver 查询仍因权限拒绝，driver
版本未覆盖。

随后在同一旧 package、Windows“平衡”电源方案下完成另一组五轮 `-NoAudit`。near 0/30/60 与
Campaign 的 whole-loop median 五轮范围/中位轮分别为 `19.296--20.004/19.903 ms`、
`94.355--100.644/97.359 ms`、`133.752--156.078/148.916 ms`、
`25.728--26.021/25.902 ms`；near 60 的 P95/P99 仍横跨
`171.969--442.528/192.907--450.468 ms`，双态没有因“节能”切换为“平衡”而消失。该组
near 60 慢帧为 27 帧原 `slot_or_fence_wait`、3 帧 `external_scheduler`，bridge 仍固定为 12 次、
88,473,600 bytes。一次从“高性能”运行中途切换到“平衡”的五轮目录不满足固定电源条件，明确不作为
性能证据。driver 版本仍未覆盖，因此仍不得退出 RB-0。

为继续拆分该双态，低扰动统计已将原笼统 wait 拆为 frame-slot render-fence recycle、graphics submit
fence、presenter reacquire completion 与 acquire/present API；同时输出 GPU Raster/Draw/bridge 的
median/P95/P99、未覆盖时间和 GPU timestamp frame ID。GPU timestamp 由双帧 slot 延迟回收，退出汇总
按 frame ID 回填到对应 CPU frame，末尾尚未回收的 timestamp 不进入 GPU 分位数。reason 现在比较
同帧 GPU Raster 与各 CPU/wait 候选；未覆盖时间只表示 whole-loop 与最大单项的差，不是可相加的互斥
时间分解。

退出条件：

- 固定负载的命令计数和 simulation tick 可重复；不同轮次的差异能由审计字段解释。
- 最慢 5% 帧可分类为 Raster workload、CPU producer、slot/fence wait、present/acquire 或外部审计/调度，
  不再只得到一个笼统的 `present_wall_ms`。
- Intel 与 RTX 3050 使用同一命令完成至少 near 0/30/60、Campaign 和 presenter-300；条件允许时补 AMD。
- Quick/Full、零 fallback/readback/CPU framebuffer copy/hot queue-idle 合同继续通过。
