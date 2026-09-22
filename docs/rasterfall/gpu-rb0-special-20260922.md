# RB-0 专项失败修复与重新采样

> 文档更新：2026-09-22
> 源码核对基线：`79b405d` 加未提交工作区；RB-0 最终 Full/专项签收及 SKY 轴向朝向合同修复

本页接续 [上一轮修复](gpu-rb0-repair-20260922.md)。仍限于 RB-0，不进入同步优化或 producer 迁移。

## 专项失败根因

segmented 精确比较的像素 0 期望 `ff102030`，实际为后续毒化输入中的 `ffabcdef`。
分段命令到 final 才一起提交；raw 接口每段重传却覆盖同一 host-visible command buffer，使较早录制的
CLEAR 也读取后来的值。原测试断言和毒化输入均保留，没有调整容差。

backend 在录制过程中重传输入前，将旧 command、tile offsets/indices、texture descs/texels
backing 保留为版本链；GPU 工作完成后的新录制或销毁才释放。正常 mixed executor 仍使用冻结帧的
preflight/upload reuse，不产生 raw 版本链；提交数量、顺序与正常同步边界不变。

真正加载 validation layer 后，还暴露 Graphics resize 销毁 image/view 而 Raster 仍引用旧附件。
`tmp/rb0-segment-validation2.*` 包含非法 image/view 和 command-buffer 状态；该次异常退出，用户看到
弹窗并终止，明确不计通过。Graphics 现在维护 color attachment 借用关系：resize 等待已提交借用帧，
废弃未完成录制、清除续画有效性，对匹配 extent 重绑定；销毁任一方都解除关系。下次必须重新 CLEAR。
raw 门禁增加 resize 后 LOAD/Draw 拒绝，以及先销毁 Graphics 后重新开始纯 Raster 帧的精确比较。

## 专项入口与证据

`tools/gpu_rb0_special.ps1` 替代 tmp 准备脚本。默认串行执行全部专项；
`-Stage Validation/Faults/Soak` 可独立复核，manifest 记录所选阶段、package SHA 和进程退出码。
本地 layer 默认位于 `tmp/hg2a-tools/mingw64/bin`，可用 `-ValidationLayerDirectory` 指定。
脚本补齐 layer DLL 依赖 PATH，并要求 loader 插入日志及 `CURRENT-VALIDATION-ENABLED` 的
Synchronization 项，不能把加载失败视为验证通过。

故障门禁核对 frame=10 的实际注入。record/submit failure 必须退出 3、attempted=10/rendered=9、
此前帧零回退；present-out-of-date 只允许故障帧 poisoned，下一帧必须进入新 generation 并恢复。
正常帧仍禁止 poisoned/hot queue-idle/readback/CPU copy。

当前已完成证据：

- `tmp/rb0-segment-final-validation.out/.err`：原精确比较和新增生命周期断言通过，Core/sync validation 开启，零 VUID/SYNC-HAZARD。
- `tmp/rb0-special-final-20260922`：timestamp、native 四 extent resize、300 帧 validation 成功；后续因脚本错误预期退出码停止，整个 manifest 不称 PASS。
- `tmp/rb0-special-faults2-20260922`：五类故障全部 PASS；之前脚本判定错误的现场保留。
- timestamp 容量不足为 requested=27/recorded=16/dropped=11/valid=0；扩容后为 27/27/0/1。

同一 package 已完成 `tmp/rb0-special-soak-20260922` 的 10,000 帧零回退/零 readback/copy，
`tmp/rb0-special-quick-20260922` 与 `tmp/rb0-special-full-20260922` 均 PASS。
package 大小 14,373,168 bytes，SHA-256 为
`E932E59366F058F315DDFCB3EB1284BA3A8CF5A26FB252A19CEA0F15BAE76AD1`。

## 画面与命令复核的边界

Full 的 near/mid Raster replay 均颜色、深度零差异。实际流分别包含 15,902/15,121 条命令，
种类统计和 SHA 保存在 `tmp/rb0-command-content.json`；这验证的是捕获到的 Raster 流，不能证明
native mixed 所有内容与 CPU 一致。生产 actor flush scope 的命令内容、顺序回归也由本次 logic 门禁复核。

`tmp/rb0-special-visual-20260922` 在相同 fixed tick、第 30 帧分别采集 near/mid CPU/native。
原 `comparison.png` 显示 native 天空为深色 clear，世界血条只剩文字，远处围墙也比 CPU 明亮。
后续 RB-0 归因确认：mixed frame 未携带 Core 已有的 SKY 快照；血条矩形直接写 overlay color，未通知
coverage observer，因而未被 native composite 消费。现已把相机 SKY 参数冻结到 mixed frame，并在 pack 后
将命令 0 替换为既有 `SKY_V1`；私有矩形 helper 改走 `fb_fill_rect()`，CPU 像素语义不变但同步记录 coverage。
`tmp/rb0-visual-fix-20260922` 使用新 package 在同一条件重采，near 天空主体逐像素零差异，near/mid 的
名字、世界血条和 BASE 条形均可见。远墙明暗差异仍存在：CPU planar 路径按相机距离应用
`baked_fog_at()`，persistent-map Graphics Draw 没有等价 fog 参数；在 RB-0/RB-1 前不得为此扩大 Graphics。
因此仍**不宣称完整画面对照通过或 RB-0 退出**。`--dump-frame` 实际输出 PPM，即使文件名是 `.bmp`；
组图前只转换容器格式，不修改像素。

该画面修复后的 Windows build/package、完整 logic test 与 GPU Quick 均通过；Quick 证据目录为
`tmp/rb0-visual-fix-quick-20260922`。该阶段 package 为 14,373,884 bytes，SHA-256
`4A0FDDBA36B7150B657DBBECB8971F9A1621EDBE4BB168BDEBEB21A6B3C15BD0`；最终签收包见下文。
`--dump-frame` 实际输出 PPM，即便文件名是 `.bmp`；组图前只转换容器格式，不修改像素。

## 性能证据 package 的正式五轮

审计组 `tmp/rb0-special-audit5-20260922` 与低扰动组
`tmp/rb0-special-noaudit5-20260922` 均完成五轮，各 20 次运行，脚本 PASS；两组 manifest 记录的 package
SHA 为 `E932E59366F058F315DDFCB3EB1284BA3A8CF5A26FB252A19CEA0F15BAE76AD1`。其后仅有 SKY/overlay
画面 coverage 修复，当前最终 package SHA 为上一节的 `4A0F...BD0`；不得把两者写成同一二进制。
Windows native required、1280×720、固定 simulation tick、平衡方案、ACLineStatus=1，GPU 严格串行。
完成后 package SHA 未变，无遗留 Rasterfall/GPU 测试进程。没有执行提交。

低扰动 whole-loop（ms）如下，中位轮是五个 median 的中位数：

| 场景 | median 范围 / 中位轮 | P95 范围 | P99 范围 |
| --- | ---: | ---: | ---: |
| near 0 | 16.209–17.687 / 16.432 | 19.436–27.256 | 19.600–33.525 |
| near 30 | 30.334–32.348 / 31.233 | 36.466–41.690 | 37.555–49.287 |
| near 60 | 38.388–43.896 / 40.296 | 45.258–54.805 | 47.806–59.508 |
| Campaign | 24.629–26.066 / 25.280 | 26.294–29.638 | 27.325–55.328 |

本组没有复现旧记录中数百毫秒的 near60 跨轮双态，但旧 workload 错误且 whole-loop 口径不同，
不能据此计算“优化收益”，也不能宣称历史双态根因已经闭合。

审计四场景各自五轮的逐帧规范化 workload hash 完全一致。预热后 near 的 bridge 固定为
12 次、88,473,600 bytes；本组 Campaign 固定为 14 次、103,219,200 bytes，不能套用旧组的 14/16 状态。
每组预热后共有 3,080 个 CPU 样本，3,040 个完整 GPU timestamp；各次末尾两帧尚未回收，不计 GPU
分位数。低扰动全部 incomplete=0、dropped=0；near max_requested=27，Campaign max_requested=32。

真实工作负载与归因：

- near60 预热后 enemy-body opaque 命令为 21,974–30,415，中位 26,549；weapon 中位 1,528，
  gear 中位 768。旧 weapon 约 97K 模式消失。数量支持将 body 作为后续候选，不能代替逐 producer GPU 耗时。
- 审计 near60 CPU render median 为 20.096–20.958 ms；其中 enemies 为 12.486–12.979 ms，
  AI teammates 为 4.500–4.691 ms。execute median 为 12.593–13.396 ms，包含嵌套细分与等待，不能再相加。
- 低扰动 near60 完整 GPU Raster median 为 23.318–25.449 ms，Draw 为 1.422–1.447 ms，
  bridge 为 3.124–3.187 ms；这些 GPU 分类不与 CPU wall 直接相加。
- 审计全部预热样本只有 skinning caller 出现同步 submit，每帧一次；bridge caller 为零。
  3,080 个 CPU 样本的最新未确认前序帧均为 N-1，其中 3,060 个能配对到该前序帧的完整 GPU timing。
  near60 的 graphics wait 与前序 GPU Raster 相关系数跨轮约 -0.002–0.278，不能据此建立时间分摊因果。
- 低扰动 near60 最慢 5% 中，19 帧为 cpu_producer、6 帧为 gpu_raster_workload、5 帧 unclassified。
  frame-slot wait P95 为 0.006–0.012 ms；这些慢帧的 skinning wait 为 0.166–7.978 ms。
  没有证据继续把旧数百毫秒长尾归为 bridge fence。

审计 whole-loop 不包含其后的逐帧日志时间；日志会给上一 GPU 帧更多完成时间，因此审计和低扰动组
仍必须分开解释。离线汇总保存在 `tmp/rb0-special-analysis.json`，复核脚本为 `tmp/rb0-analyze.py`。
慢帧分类只是最大已测信号的启发式，不是因果证明。

## schema 5 性能归因结论

离线复核只使用上述正式五轮和 `tmp/rb0-special-analysis.json`。低扰动最慢 5% 共 170 帧：

| 场景 | CPU producer | GPU Raster | unclassified |
| --- | ---: | ---: | ---: |
| near 0 | 1 | 0 | 29 |
| near 30 | 3 | 21 | 6 |
| near 60 | 19 | 6 | 5 |
| Campaign 320 | 5 | 60 | 15 |

55 个 `unclassified` 并非 whole-loop 没有顶层覆盖。每帧的 `prepare + render + execute + remainder`
均闭合 whole-loop；缺失的是 phase 内部可建立因果的子计时。按最大互斥 phase 观察，near60 的五帧中
四帧由 render 主导、一帧由 prepare 主导；五帧最强已测信号均为 CPU producer（20.811--27.725 ms），
但只占整帧 41%--50% 且没有达到分类阈值，必须继续保留 `unclassified`。其余场景的未分类样本也分散在
prepare/render/execute，不能统一改名为 scheduler、GPU wait 或 producer。低扰动日志没有 phase 内更细的
prepare/execute wall breakdown，审计日志虽有 renderer 子项，但逐帧输出会改变调度，不能回填为低扰动因果。

历史 near60 数百毫秒跨轮双态在正确口径五轮中未复现：五轮 P95 为 45.258--54.805 ms，P99 为
47.806--59.508 ms；每轮最慢 5% 的中位数为 46.956--58.237 ms，只有第五轮单帧最大值 76.709 ms，
不构成旧记录的数百毫秒双态。可证伪候选如下：固定 workload hash、bridge 次数/字节、timestamp 截断、
frame-slot wait、present/acquire API 和单一 graphics fence wait 均不能解释旧双态；电源方案只能影响幅度，
旧资料也已证明它不是充分原因。仍未实测的是低扰动 prepare/render/execute 内部的调度停顿、OS/驱动抢占
以及同一时段的频率/温度遥测。因此结论是：当前正确口径未复现，历史根因未知；作为已知风险冻结，
不能写成根因闭合，也不阻止完成剩余 RB-0 签收。

## 最终签收

最终 Full 首次运行在 `thin-far` 稳定暴露 SKY 校验回归：合法轴向视角为 `direction=(-1024,0)`，
而 Raster ABI validator 错误要求 `direction_cy` 必须非零。合同已修正为 yaw/pitch 各自的 sin/cos
不能同时为零，允许任一分量为零；pack 单元门禁增加 `sy=-1024, cy=0` 通过及零向量拒绝。
Vulkan Raster 与 mixed preflight 同时补充提交前失败原因，不改变正常判定或提交顺序。

修复后的最终 Windows package 为 14,375,420 bytes，SHA-256
`F407FD19BFC1FBC049ADE78EA21EB9E71D7CD2B627C0CAD388CB1DC8E363D762`。平衡电源方案、Intel Iris Xe、
GPU 严格串行条件下：

- `tmp/rb0-final-full2-20260922`：Full 31/31 PASS，覆盖 logic、Raster differential、mixed/native resize、
  near/mid、thin-far、presenter-300、world-cycle、Campaign、near30/60、character rollback 与固定 capture。
- `tmp/rb0-final-special-20260922`：All PASS；timestamp 截断/扩容、resize validation、300 帧
  synchronization validation、五类 fault 和 10,000 帧 soak 全部符合合同。
- validation 输出中 `VUID-`、`SYNC-HAZARD`、`ERROR` 命中为零；soak 保持 required native，零 fallback、
  readback、CPU framebuffer copy 和 hot queue-idle。
- 结束时 package SHA 与专项 manifest 一致，活动电源仍为平衡，无遗留 Rasterfall/GPU 测试进程。

Intel 单设备上的 RB-0 实现、测量、性能归因和最终回归现已签收。fog/远墙差异与历史双态按前述决定冻结；
RTX 3050/第二物理 GPU 仍按用户要求暂缓，因此不得把当前结论写成跨设备签收或直接进入 RB-1。

## 后续边界

专项失败已处理，Quick/Full、两组五轮已完成，勿再次把 11:30 交接中的 raw memcmp 当当前阻点。
性能归因、最终 Full/专项回归及文档签收已经完成。fog/远墙差异按用户决定冻结；`unclassified` 与历史
双态按上节作为根因未知的已知风险冻结。剩余只有 RTX 3050/第二物理 GPU（按用户要求暂缓）和是否提交。
保持平衡方案、Windows PowerShell、GPU 串行；不进入 RB-1，不合并 span/跳 bridge/迁移 producer，
不扩展 Graphics 类型，不提交，不跑 update-bootstrap。

## 测量工具和环境

`gpu_metrics.ps1` schema 5 读取七类真实 graphics submit/wait 与 prepare/render/execute/remainder
互斥 CPU phase，检查 phase 合计；按 frame ID 配对完整 GPU timing，保留当前 wait 的前序 Raster frame
timing。前序关系是整 CPU frame 内最新未确认完成帧，不是逐 submit 时间分摊。
慢帧分类不再用 frame_interval 与上帧 whole-loop 的差值作为本帧原因，也不再用
`slot_or_fence_wait`/`audit_or_scheduler` 名字推断因果。最大候选不足 whole-loop 一半时归为
unclassified。audit 无 slot wait 字段，需查看独立低扰动 RB0 stats，不补零冒充实测。

采样脚本记录 package SHA，在各运行边界核对平衡方案和 Win32 ACLineStatus，结束时复核 package。
`-DriverLibraryPath` 可记录 validation loader 所指向的驱动 DLL 版本/哈希。
本机实际 loader 路径为 `C:\Windows\System32\DriverStore\FileRepository\iigd_dch.inf_amd64_775693c40ecf0aa2\igvk64.dll`，
文件版本 `32.0.101.6314`。`tmp/rb0-driver-version.json`、`tmp/rb0-power-source.json` 保存查询结果；
ACLineStatus=1，平衡方案不变。CIM 枚举仍不可用；RTX 3050/第二物理 GPU 仍按用户要求暂缓。
