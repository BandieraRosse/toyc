# GPU 当前状态页的历史测量记录

> 归档：2026-09-22
> 以下为修复前后阶段记录，阻塞条件只代表当时现场。
> 当前状态见 [GPU 当前状态](../gpu-current-state.md)。

2026-09-22 的当前 Intel package 已完成两组各五轮。审计组确认 near 0/30/60 的 bridge 固定为 12 次、
88,473,600 bytes，Campaign 为 14/16 次、103,219,200/117,964,800 bytes；无审计组单独保存最终 stats。
用 schema 4 重算审计组后，near 0/30/60 与 Campaign 的五轮逐帧规范化 workload 均逐场景完全一致；
Campaign 的 14/16 bridge 状态是确定帧序列的一部分，不是跨轮调度漂移。
逐帧 producer/bridge 输出会明显扰动重负载调度，因此两组数据不得直接拼接或用审计组 whole-loop 作为
优化收益基线。`--gpu-rb0-stats` 在前 16 帧预热后只向内存记录 whole-loop、Raster/GPU Raster、CPU
producer、slot/fence wait、acquire/present、producer 与 bridge 数字，退出时一次输出分位数和最慢 5%；
`gpu_rb0_sampling.ps1 -NoAudit` 使用该结果作为低扰动基线。本机 CIM 电源、adapter/driver 枚举因权限拒绝，
脚本另记录低权限 `powercfg /getactivescheme` 结果；另一物理 GPU 五轮复核仍未覆盖。

最新 package 的 `--gpu-rb0-stats` 格式门禁和正式五轮低扰动采样均通过。预热后 whole-loop median
的五轮范围/中位轮为：near 0 `24.660--28.735/25.262 ms`，near 30
`100.004--110.014/106.324 ms`，near 60 `146.750--203.240/157.118 ms`，Campaign
`27.869--31.301/30.804 ms`。near 60 的 P95/P99 范围扩大到
`174.307--583.242/178.815--641.747 ms`；其中 18/30 个最慢 5% 帧归为 `slot_or_fence_wait`，其余
12 帧归为 `external_scheduler`。对应慢帧的 bridge 仍固定为 12 次、88,473,600 bytes，因此该双态
不是 workload 或 bridge 计数漂移。near 30 的慢帧为 13 帧 `slot_or_fence_wait`、17 帧
`external_scheduler`；near 0 全部为 `external_scheduler`；Campaign 主要为 `external_scheduler`，仅
4 帧为 `cpu_producer`，没有慢帧归为 `raster_workload` 或 `present_or_acquire`。本轮活动电源方案为
“节能”，driver 版本仍因低权限枚举失败而未覆盖；这些结果只补强 RB-0 的长尾归因，不能据此进入
RB-1。

同一旧 package 在“平衡”方案下的后续五轮仍复现 near 60 双态：whole-loop median 范围/中位轮为
`133.752--156.078/148.916 ms`，P95/P99 范围为
`171.969--442.528/192.907--450.468 ms`。near 60 最慢 5% 中原分类为 27 帧
`slot_or_fence_wait`、3 帧 `external_scheduler`，bridge 继续固定为 12 次、88,473,600 bytes；因此不能把
节能方案视为双态的充分原因。一次运行中途由“高性能”切换到“平衡”的目录已判为无效，不参与对照。

当前低扰动统计进一步区分 frame-slot recycle、graphics submit fence、presenter reacquire completion
和 acquire/present API，并输出 GPU Raster/Draw/bridge 分位数、未覆盖时间及 CPU/GPU frame ID 对应。
双帧延迟返回的 GPU timestamp 按 frame ID 回填到对应 CPU 样本，末尾未回收样本不进入 GPU 分位数；
reason 可比较同帧 GPU Raster。未覆盖时间不是互斥时间分解。该统计合同已通过 Windows build、logic、
Quick、Full 与四场景一轮格式门禁；随后完成的正式五轮归因和最终专项签收已结束 Intel 单设备 RB-0。
