# RB-0 排查：命令生命周期与计时覆盖

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../gpu-rendering-architecture.md)

> 文档更新：2026-09-22
> 源码核对基线：`79b405d` 加当前 RB-0 工作区；Windows package 2026-09-22 10:40:58

## 结论与推进顺序

后续修复已进入工作区，当前进展、验证结果和新会话任务见
[RB-0 修复与续接](gpu-rb0-repair-20260922.md)。下文保留修复前调查现场；其中“本次”指原调查轮。

本次只排查、采样与更新文档，没有修改运行时代码，也没有进入 RB-1。
下一步首先修复 actor 命令范围跨 flush 失效和计时覆盖，再建立性能基线。当前 producer 计数虽然跨轮
可重复，却包含被重新纳入提交的旧命令，不能据此决定先迁移 weapon，也不能证明 RB-0 只增加了观测。

建议顺序：

1. RB-0 correctness：使 actor 局部裁剪明确识别命令缓冲的 flush/generation 边界；禁止用旧起点扩大
   `cmd_count`。测试既要覆盖 `begin > end`，也要覆盖 flush 后新命令数再次超过旧起点的情况；
   仅添加大小比较不能完整解决范围身份问题。保留画面、顺序和权威玩法语义，检查命令实际内容与数量。
2. RB-0 measurement：query pool 按执行计划保证容量，输出 requested/recorded/dropped 及完整性；
   对 graphics wait 按实际调用者分类，将提交帧与其前序未完成帧关联；增加互斥的 CPU 顶层阶段。
3. 同 package 重做固定 workload 的审计与低扰动五轮、CPU/reference 图像核对和 Quick/Full。
   先确认观察代码没有引入额外提交，再讨论优化收益。RTX 3050 仍暂缓。
4. 通过 RB-0 后，RB-1 优先评估每帧动态资源创建/蒙皮同步提交造成的队列串行化，以及 depth bridge
   往返；以完整计时和消融实验排序。这不等同于优化 skinning shader 算术。
5. RB-2 迁移真实高成本 opaque producer；enemy body 是候选，weapon/gear 的排名必须重算。
   透明、VFX、新材质和 Draw 微优化继续不进入本轮。

## 发现一：actor 裁剪会恢复已消费的命令范围

调用链：

`render_ai_teammate()` 保存 `actor_command_start = renderer->cmd_count`
→ `render_modular_ai_teammate()` 切换 body/gear/weapon producer
→ `rf_core_mixed_set_producer()` 调用 `toy_renderer_flush()`
→ consumer 将命令复制进 retained frame，`cmd_count` 清零
→ `audit_ai_actor_screen_commands(renderer, actor_command_start)` 使用旧起点。

最后一个函数以 `write = begin` 初始化，并无条件执行 `renderer->cmd_count = write`。
若 flush 后 `end < begin`，循环完全不运行，却将计数抬回 flush 前的值；后续 flush 会再次收集缓冲中的
旧槽位。槽位内容可能包含旧命令和本次覆盖的新命令，不能把它描述成四份逐字节相同的 enemy stream。
即使 `end >= begin`，跨 flush 的起点也不再标识原 actor 的范围。

本地隔离复现 `tmp/rb0-command-range-repro.c` 使用当前函数原文及最小结构定义，未调用 GPU：

```text
flushed case: before=400 begin=24364 after=24364 resurrected=23964
REPRODUCED: stale pre-flush range resurrects consumed command slots.
```

编译使用 Windows MinGW GCC，`-Wall -Wextra -O0`，运行退出码 0（表示成功复现缺陷）。
这是函数级复现，不是修复后的集成验收。源码位置在 `rasterfall_render.c` 的
`render_ai_teammate()`、`render_modular_ai_teammate()` 和 `audit_ai_actor_screen_commands()`；
消费合同在 `rf_core_mixed_frame.inc` 与 `lib/graphics/renderer.c`。

现有旧审计 `tmp/gpu-rb0-sampling-20260922-084758/round-01-near-60.runtime.log` 首帧中，
enemy-body 为 24,364，weapon 为 97,456，weapon spans 为 4。本次三个 near 场景共 90 条慢帧记录全部
满足 `weapon == 4 * enemy-body`，与四个 modular actor 触发上述失效范围的路径吻合。
旧 workload hash 一致只能证明错误负载可重复，不能证明其正确。

## 发现二：GPU timestamp 只覆盖前 16 个区间

`gpu/src/rf_gpu_vulkan_backend.c` 的 query pool 创建/reset 固定为 32 个 query；
`timestamp_begin()` 在 `timestamp_count > 30` 时直接返回 `UINT32_MAX`，随后不再写时间戳。
`timestamp_collect()` 仍设置 `valid=1`，没有报告截断。

每个正常 mixed Draw batch 在 `gfx_render()` 中记录 import、Draw、export 三个区间。
near 的 6 个 Draw span 仅这部分就需要 18 个区间，尚未包含穿插的 Raster、末尾 Raster、Post、overlay
和 present copy。Campaign 的 7/8 个 Draw span 同样超过容量。因此当前 GPU Raster/Draw/bridge
数字是已记录前缀的分类合计，不是完整帧总量；`valid` 与 CPU/GPU frame ID 相等不代表覆盖完整。
GPU timestamp 的 TOP/BOTTOM 边界还可能包含流水线依赖等待，不应直接将各分类相加为互斥耗时。

## 发现三：graphics fence 不是 bridge 专属等待

`rf_core_host.c` 的 `mixed_graphics_wait_ms` 来自 graphics 全局累计
`fence_wait_wall_ms` 的帧内差值。唯一累加位置是 `gfx_submit()`，所有调用者共用。

正常 mixed 的 `gfx_render()` 将 import/Draw/export 录进 Raster frame command buffer；
`gfx_bridge(..., record_only=1)` 不调用同步 graphics submit。
相反，preflight 每帧重建动态角色资源，`rf_gpu_graphics_skinned_resource_create()` 在独立 command
buffer 中 dispatch skinning，随后 `gfx_submit(g,1)` 等待同一 queue 上的 fence。资源 staging fallback
和 vertex diff 诊断也能进入同一计数器。旧审计的 near 稳态记录为每帧一次 graphics submit/wait，
与正常蒙皮路径一致，并非每次 bridge 一次 wait。

因此 frame N 的 wait 可以受先前提交的 frame N-1 工作影响；按 frame ID 回填 N 的 timestamp 是必要的
样本关联，但不足以解释 N 的等待原因。应在真实 submit 调用点分类并关联前序帧，不应只在
Raster→Draw import / Draw→Raster export 的录制边界拆 CPU fence。

`external_scheduler` 也只是当前分类器在最大候选不足 whole-loop 一半时设置的兜底字符串，
不是对操作系统调度的测量。`unattributed_us = whole - max(candidate)` 不是未测 CPU 时间或互斥余项。

## 新五轮证据

证据目录：`tmp/gpu-rb0-sampling-20260922-105728`。命令：

```powershell
powershell -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 5 -NoAudit
```

固定 simulation tick、1280×720、Intel Iris Xe、Windows 原生 required gpu-native；near 各 120 帧，
Campaign 320 帧，预热 16 帧。全程保持平衡方案 `381b4222-f694-41f0-9685-ff5bb260df2e`，GPU 严格串行。
20 次运行全部退出 0，脚本 PASS，零 CPU fallback/readback/framebuffer copy，结束后无遗留进程。
这组可以作为当前有缺陷实现的复现证据，不能作为预期负载的优化收益基线。

HEAD `79b405da6312abd73b3eaea5e78c7cb12a784f01`，工作区非 clean，详见 manifest。
exe 大小 14,364,948 bytes，时间 `2026-09-22T10:40:58.9805705+08:00`；采样后 SHA-256：
`5A2CEDA811E7BB9DF3DF025366228530DF8AE928B60E3ED33C16867556F14FAD`。

单位 ms，范围为五轮统计量的最小至最大值，“中位轮”为五个 median 的中位数：

| 场景 | whole median 范围 / 中位轮 | whole P95 范围 | whole P99 范围 |
| --- | ---: | ---: | ---: |
| near 0 | 19.477–20.484 / 19.777 | 21.756–27.382 | 23.639–57.385 |
| near 30 | 92.370–98.270 / 94.165 | 115.229–167.123 | 118.456–228.501 |
| near 60 | 139.152–148.599 / 143.154 | 164.867–414.438 | 169.567–525.616 |
| Campaign | 25.614–26.246 / 25.754 | 33.947–35.789 | 36.396–43.266 |

near 60 的逐轮比较如下。GPU Raster 列受上述截断限制，只能观察已记录前缀的变化：

| 轮次 | whole P95 | GPU Raster P95（部分覆盖） | graphics fence P95 |
| --- | ---: | ---: | ---: |
| 1 | 414.438 | 222.274 | 339.259 |
| 2 | 166.599 | 68.766 | 86.397 |
| 3 | 264.828 | 138.714 | 183.395 |
| 4 | 166.712 | 69.075 | 83.295 |
| 5 | 164.867 | 67.503 | 84.741 |

GPU 前缀耗时和 fence 的跨轮变化方向一致，但分位数不代表同一个样本，更不证明同帧因果。
near 60 的 slot wait P95 为 0.005–0.008 ms，P99 为 0.010–0.041 ms；acquire/completion P95 为
0.013–0.022 ms，present API P95 为 0.047–0.058 ms。就本次数据，直接等待帧槽和 presenter API
不是数百毫秒长尾的主要位置；不能由此排除显示系统或驱动对 queue 执行的间接影响。

near 60 的部分 GPU Raster median 为 59.877–61.795 ms，部分 Draw median 为 1.322–1.346 ms，
部分 bridge median 为 2.101–2.131 ms。`unattributed` median 为 67.971–75.297 ms，
P95 为 85.098–91.162 ms；不能据此声称有这些毫秒的 OS 调度时间。

170 条最慢 5% 的原分类：

| 场景 | graphics_fence_wait | external_scheduler（兜底） | cpu_producer |
| --- | ---: | ---: | ---: |
| near 0 | 0 | 29 | 1 |
| near 30 | 28 | 2 | 0 |
| near 60 | 14 | 16 | 0 |
| Campaign | 11 | 68 | 1 |

有 GPU timing 的慢帧 frame ID 全部匹配；near 60 两条末尾慢帧无回收 timing，不能当作 GPU 用时为零。
near 慢帧 bridge 均为 12 次、88,473,600 bytes；Campaign 慢帧中 5 条为 14 次、103,219,200 bytes，
75 条为 16 次、117,964,800 bytes。这仅核对本次输出的慢帧；本次 NoAudit 不提供全帧 workload hash。

## 验证边界

本次完成新统计 package 的五轮运行、原始日志复核、代码追踪及隔离 C 复现；没有重建 package，
没有重跑 Quick/Full，也没有把交接中的既有 Full 当作本次验证。当前正确性缺陷说明既有通过项不足以
验证 flush 前后命令范围，修复时需要加入该语义门禁。

Intel driver 版本和电源供电来源仍因 CIM 权限缺失未覆盖；RTX 3050/第二物理 GPU、跨设备 presenter-300
仍暂缓。near 60 双态的全部机制尚未闭合，不能把命令复活直接宣称为跨轮双态的唯一原因。
RB-0 未退出；现在已有足够证据确定先修复负载与测量，再决定同步和 opaque 迁移的开发顺序。
