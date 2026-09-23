# RB-0 修复进展与续接（2026-09-22）

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)

后续专项根因、修复与实机验证见 [RB-0 专项续接](gpu-rb0-special-20260922.md)。本页保留 11:30 交接现场。

> 文档更新：2026-09-22
> 源码核对基线：`79b405d` 加未提交工作区；package SHA-256 `99E1932B9C2F8C21389D6A9AF7CE8C6E7F67AE082372B535BEA9DB739085D374`

用户要求快速收尾并交给新会话。本轮已修改运行时代码并完成 Windows build/package、logic、Quick/Full；
**尚未重建正式性能基线，RB-0 未完成，不进入 RB-1。** 原缺陷证据保留在
[调查报告](gpu-rb0-investigation-20260922.md)，本页记录其后的修复与验证边界。

## 已实施

- `toy_renderer.command_filter` 在每次 flush 的 observer/consumer 之前执行可变压缩。
  `rasterfall_render.c` 的 actor scope 记录本段起点，flush 前裁剪本段，随后将起点重置为零；
  modular/procedural 调用结束时裁剪剩余尾段并解除 callback。作用域累计 generated/retained，
  避免以跨 flush 的 `cmd_count` 差值判断零命令 actor。没有新增 flush、合并 span 或改变提交顺序。
- `rasterfall_draw_reference_test.inc` 用真实 `toy_renderer_flush()` 和 production filter 比较消费到的
  命令内容、顺序、前缀保护；覆盖无 flush、多次 flush，以及新命令数小于/等于/超过旧起点。
- Vulkan timestamp pool 改为可扩容；mixed preflight 在 slot recycle 后按非空 Draw span 的上界
  `4 * draw_spans + 4` 预留区间（preceding Raster、import/Draw/export、final Raster/Post/overlay/copy）。
  requested/recorded/dropped 随 GPU frame ID 回填；截断时 valid=0，不进入有效 GPU 分位数。
- `gfx_submit()` 按 upload、vertex-diff、skin-input、skinning、bridge、draw、readback 七类累加 submit/wait。
  记录本次 CPU frame 与提交时尚未确认完成的最近 Raster frame。成功 fence/recycle 更新已确认完成的
  frame 水位。该 predecessor 是队列先后关系，不等于已测得其整个 GPU 时间都落在当前 CPU wait 内。
  同一 CPU frame 多次 submit 的 predecessor 取已观察到的最近未确认帧；不是逐 submit trace。
- 低扰动 whole_us 原先是 `now - t_frame`，少算输入/update，现与逐帧审计统一为
  `now - audit_loop_start`。prepare/render/execute/remainder 为互斥顶层阶段，execute 包括
  `rf_core_end_frame()` 内 freeze/preflight/提交/present；已有细分计时仍可能嵌套。
  `unattributed_us` 仍是 whole 减最大候选，不能当作互斥 CPU 余项；兜底 reason 改为 unclassified。
- `RB0-COVERAGE` 汇总完整/截断/末尾未回收样本；sampling、metrics、acceptance 增加覆盖检查。
  metrics 会拒绝旧日志缺少覆盖元数据的 GPU timing，历史报告应使用原证据，不将旧前缀计时当完整帧。
  sampling manifest 新增 package SHA-256。

## 本轮证据

HEAD 未变；package 为 `build-windows/rasterfall-windows/rasterfall.exe`，14,371,092 bytes，
本地写入时间 `2026-09-22 11:20:56`。完整 SHA-256 见页首。

- 最终 Windows 构建：`tmp/rb0-repair-build-final.log`、`tmp/rb0-package.log`；
  differential 独立 exe 重建：`tmp/rb0-diff-build-native.log`。
- Quick：`tmp/rb0-repair-quick3-20260922`，PASS；logic 的 actor command flush scope PASS。
- Full：`tmp/rb0-repair-full-20260922`，31 个脚本运行条目全部通过，包括 logic、CPU/GPU differential、
  native resize、near/mid、thin-far、presenter-300、world-cycle、Campaign、near 30/60、vertex diff、rollback 和 capture。
- native resize 新 fixture 交错八个 Draw，回收帧 requested=recorded=36、dropped=0，GPU frame ID 正确。
- near 60 首帧 enemy-body=24,364、gear=769、weapon=1,560；旧 weapon=97,456 的复活模式已消失。
  这仍不是五轮正确负载基线。near 0 已见 requested=recorded=27，正常 graphics submit 为 skinning，
  frame N 的 predecessor 为 N-1。near GPU capture 已人工查看，完整 near/mid/reference 组图人工复核未完成。

## 必须继续处理的专项失败

`tmp/rb0-special.ps1` 已准备 timestamp、validation/sync validation、五类 fault、10,000 帧 soak 的串行脚本。
其第一次执行停在 `rf-gpu-raster-test.exe --mixed-gate`，目录
`tmp/rb0-special-20260922-112733`。失败在 `gpu/src/rf_gpu_raster_test.c:206` 的 segmented color memcmp，
尚未到新增 timestamp 截断测试。**validation、fault、soak 均尚未执行，不能称通过。**

为区分本轮回归，已从 HEAD 提取 backend、graphics.inc、raster_test 三个原文件到
`tmp/baseline-*`，使用相同当前头文件/未修改 pack/bin 源编译 `tmp/rb0-baseline-raster-test.exe`。
原测试也在同一 segmented memcmp 断言失败，日志 `tmp/rb0-baseline-raster-result.log`。
这证明该失败可在本轮 runtime 修改之前的实现复现，但根因尚未查明；不是完整 clean checkout 的基线。
下一会话先排查该合同或将新增 timestamp fixture 独立出来，不能删除/放宽原断言冒充通过。

另一个旧门禁问题已修正：重建后的 hosted graphics 仍断言同设备另一 graphics owner 不可 bind，
与当前公开的同设备帧槽共享合同相反；已改为允许 bind，仍要求非创建者 destroy 失败。
第一次 Quick 留在 `tmp/rb0-repair-quick-20260922`；第二次 Quick 的新八 Draw fixture 忘记同步
require_draws，已修正为八，证据 `tmp/rb0-repair-quick2-20260922`。最终有效门禁为 quick3/Full。

## 新会话执行顺序

1. `git status --short`、检查所有相关 GPU 测试进程和 rasterfall、`powercfg /getactivescheme`；
   保留全部已有修改，不提交。维持平衡方案；RTX 3050 继续暂缓。
2. 处理上面的 segmented 专项失败，运行新增“耗尽容量 invalid / 扩容完整”测试。
   `tmp/rb0-special.ps1` 的 validation layer 位于 `tmp/hg2a-tools/mingw64/bin`；脚本要求 loader 日志
   证明加载且没有 VUID/SYNC-HAZARD。该脚本尚未跑到验证 loader 输出匹配的分支，可能需据实际日志调整。
3. 补 validation/sync validation、fault injection、soak。若再改 runtime，重建 package、Quick/Full。
4. 同一最终 package 分别运行审计五轮和低扰动五轮：
   `powershell -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 5`；
   `powershell -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 5 -NoAudit`。
   两组分目录，严格串行；补画面/命令内容核对，检查 timestamp 全覆盖、前序 frame 和各 submit 调用者。
5. 新旧 whole-loop 口径不同，且旧 workload 错误，不能把差值直接当优化收益。
   用新五轮重新排序真实 opaque producer，决定未来 RB-1 同步诊断方向，本轮不实施同步优化或迁移。

尚未覆盖：正式新 package 五轮、near 60 跨轮双态闭合、上述专项、完整人工图像对照、Intel driver
版本/供电来源（CIM 权限失败）、第二物理 GPU/跨设备 presenter-300。审计 metrics 的慢帧分类器仍沿用
旧 `slot_or_fence_wait`/`audit_or_scheduler` 名称，未接入本轮七类提交和互斥 CPU phase；新原始日志字段
已存在，分析时应显式读取或继续完善工具，不按旧名字推断因果。
