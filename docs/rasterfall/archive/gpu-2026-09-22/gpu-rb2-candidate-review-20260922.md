# RB-2 候选评估与审计修补

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)

> 文档更新：2026-09-22
> 源码核对基线：`14e5fe6` 工作区，Core 零 submit 帧归属、候选汇总与普通 infected ablation 否决

## 决策

存活普通 infected body 与相邻 blob shadow 的受限 ablation 已实施、实机短测并撤销。
它证明该 producer 存在结构与耗时收益，但没有保持现有逐面光照画面，因此不能作为可接受迁移。
不恢复已否决的 special rigid-only 实验，不扩展 typed dynamic stream，不迁移死亡、舌头或 VFX。

选择依据：near 30/60 的 special rigid 命令均为零，普通 body 分类的 RasterCmd 随负载显著增长；
world/map、gear、weapon 和 viewmodel 规模相对稳定。源码确认 `character_dynamic_draw_submit()`
明确拒绝 `active_infected_model`，普通 infected 虽有 RFCHAR pose，仍走 CPU 三角形 lowering。
此候选可优先研究复用现有 skinned 输入，避免首先引入 procedural rigid 的混合顶点合同。

尚未证明的部分：现有 GPU timestamp 只测整帧 Raster，不能给出每个 producer 的 GPU 毫秒。
near 30/60 同时增加 transparent/effects，因此不能把 near 场景时间差全部归到 body；near 0 的
enemy-body 也不为零，分类包含其他角色内容。命令数用于定位候选，不是收益证明。

## 审计故障与修复

失败现场 `tmp/rb2-rigid-baseline-audit-20260922/round-01-near-0.runtime.log` 中，CPU frame 从 1
递增，但七条 graphics-submit 均是 frame=0、submits=0、wait=0。

`gfx_submit()` 只在真正独立提交时更新 backend `wait_frame`；Core 对累计计数做差后，仍复制最后一次
submit watermark。关闭 GPU skinning 时，mixed Draw/bridge 只录制命令，允许整帧没有独立 graphics
submit，所以该 watermark 不代表当前 CPU frame。这不是 GPU timestamp 的两帧延迟问题。

- `rf_core_host.c`：本帧 aggregate submit 为零时，使用当前 render frame ID，predecessor 清零。
- `gpu_metrics.ps1`：旧日志只有在完整七类 caller 的 submit/wait 全零、aggregate submit 为零，且
  watermark 不在未来时才接受旧 frame；不为此跳过活动提交的 frame 一致性检查。清除无提交帧的旧
  predecessor，避免把历史队列关系当作本帧等待原因。
- `gpu_metrics_test.ps1 -LogPath <audit.log>`：以真实日志为 fixture，回放原始输入及受控元数据损坏；
  生成物留在 tmp，原始日志不改写。

## 本轮证据

正式盘点使用同一 Windows package：
`7B3BA034919F4516158327F6A6FE15287C3F0442A04CD6222646FC7219B36970`。
Intel、1280×720、Balanced、AC、固定 tick；GPU skinning 正常开启。所有 GPU 运行严格串行。

- audit 两轮：`tmp/rb2-candidate-fixed-audit-20260922/`
- 低扰动两轮：`tmp/rb2-candidate-fixed-noaudit-20260922/`
- 机器可读汇总：`tmp/rb2-candidate-report-20260922.json`
- 修补前 package 的正常 skinning 审计：`tmp/rb2-candidate-audit-20260922/`，仅用于 workload 对比。
- 旧失败日志重算：`tmp/rb2-audit-repaired.json`

四场景各自跨两轮 workload hash 一致，也与修补前 package 的同场景序列一致。此次仅为候选盘点，
不是五轮优化收益签收。系统驱动枚举未覆盖，第二物理 GPU 仍暂缓。

下表是两轮低扰动统计的范围，单位 ms；各项分位数不能相加。

| 场景 | whole median | whole P95 | whole P99 | GPU Raster median | GPU Draw median | GPU bridge median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| near 0 | 15.104–15.688 | 20.879–20.978 | 26.593–31.327 | 4.761–5.264 | 1.654–1.829 | 1.290–1.436 |
| near 30 | 22.429–23.047 | 28.199–28.516 | 31.560–32.345 | 13.053–13.813 | 1.395–1.398 | 1.079–1.081 |
| near 60 | 32.777–34.169 | 42.690–50.221 | 55.301–60.899 | 17.034–17.414 | 1.414–1.426 | 1.087–1.102 |
| Campaign | 21.192–21.903 | 23.472–44.185 | 24.498–54.359 | 12.647–13.100 | 1.645–1.658 | 2.633–2.672 |

Campaign 的 P95/P99 跨轮波动明显，不能据此宣称长尾已稳定或已有优化收益。

预热后的 producer RasterCmd 范围如下；这些列不是 GPU 时间分摊。

| producer | near 0 | near 30 | near 60 | Campaign |
| --- | ---: | ---: | ---: | ---: |
| world-map | 970 | 970 | 970 | 2987 |
| enemy-body | 2601–2740 | 9107–16225 | 21974–30415 | 1918–3303 |
| enemy-rigid-special | 0 | 0 | 0 | 963 |
| gear | 751–797 | 674–831 | 691–853 | 345–359 |
| weapon | 1471–1785 | 1243–1825 | 1172–1811 | 1122–1135 |
| transparent | 0 | 0–6148 | 0–8863 | 10–1174 |
| effects | 0 | 54–1429 | 104–1974 | 0–120 |
| viewmodel | 1059 | 1059 | 1059 | 1059 |

## 实际 bridge 边界

near 三场景均为 2 个实际 Draw run、4 次 transfer、29,491,200 bytes；Campaign 为 5 个 run、
10 次 transfer、73,728,000 bytes。两轮各场景只有一种预热后 bridge 有序签名。
当前 color attachment 共享，审计 color_bytes 为零，traffic 为 depth：每次 7,372,800 bytes。

| 场景 / run | import 的 previous → next | export 的 previous → next |
| --- | --- | --- |
| near / 1 | world-map → world-map | enemy-body → enemy-body |
| near / 2 | enemy-body → enemy-body | enemy-body → gear |
| Campaign / 1 | world-map → world-map | world-map → world-map |
| Campaign / 2 | world-map → world-map | world-map → world-map |
| Campaign / 3 | enemy-body → enemy-body | enemy-body → enemy-body |
| Campaign / 4 | enemy-body → enemy-body | enemy-body → enemy-body |
| Campaign / 5 | weapon → enemy-body | enemy-body → gear |

endpoint 是边界相邻 producer，不是 run 内所有 Draw 或两 run 间所有 Raster 的清单。
producer_attribution 的 bridge 计数会同时归属两个端点，不能相加当作实际 traffic。
尤其不能由 enemy-body → enemy-body 推断中间没有 Raster。

源码交叉核对：`render_enemies()` 当前按 actor 提交 blob shadow、可选 Smoker tongue，再提交 infected
或 rigid body。shadow 是 `draw_quad()` 的不透明近地几何；即使颜色像半透明阴影，也不能把它当作无 depth
副作用的装饰。special/死亡分支、深度相等时的覆盖与共享 pose scratch 都限制批次重排。

## 已执行并否决的 infected ablation 方案

本节记录普通 infected body 实验实施前冻结的范围与门禁，现已由下一节的结果完成验证并否决，
**不是当前待执行计划**。当前 producer 候选是 gear/weapon 边界预检，但必须等待 M2 完成后的重新归因。

候选：COMMON/FAST/HEAVY 的存活、完全 opaque infected 模型，保留现有 pose、方向、逐面光照、受击 tint
和资源所有权。保持现有 GPU skinning，冻结每个 actor 的 palette/输入后才复用共享 instance。
shadow 纳入同一编排评估，但不预设它必须迁移；special、死亡、舌头、transparent 和 effects 保持原合同。

实施顺序：

1. 预检固定 workload 中可迁移 actor、shadow 与不可跨越的 Raster 边界。只在可证明 color/depth 等价的
   范围内组织连续 body Draw；以现有 near 两 run 为基准，优先尝试与已有 body run 合并。
2. 独立检查活体 infected 的 Q8 光照、反馈 tint、yaw、近裁剪与 skinned backing differential。
   当前 `active_infected_model` guard 还保护死亡 squash/transform 等语义，不能直接删除整个 guard。
3. 默认关闭的受限 A/B 同时测命令、实际 run、bridge、upload、GPU Raster/Draw/bridge 和整帧。
   先短测结构与正确性，出现逐 actor 新增 bridge 或未观察到 Raster 收益就撤销，不立刻扩展资源类型。
4. 若结构与短测均支持收益，双方各五轮低扰动串行交替采样；audit 独立运行。要求 near 30/60 有可重复
   整帧收益，near 0/Campaign 无显著回退，P95/P99 和上传成本不过度增加。正式收益只能由此确认。

当时的排序是普通 infected body 优先、gear/weapon 备选；下一节记录了为何该排序已经失效。
special rigid-only 维持否决。world/map 若要迁移应另做内容归因，不顺带扩大 textured/material 范围；
透明/VFX 不属于本轮。RB-3 仍等待迁移范围明确后的 CPU 长尾证据。

## 普通 infected body ablation 结果

实验开关仅放行存活 COMMON/FAST/HEAVY infected 的现有 skinned Draw 输入，并把普通 infected、special 与
opaque blob shadow 分段编排；死亡 squash/roll、舌头、transparent 和 effects 保持旧路径。near 60 的
40 帧短测中，enemy-body RasterCmd 从 24,735–26,261 降到 3,217–6,339，whole-loop median 从
31.738 ms 降到 26.667 ms，GPU Raster median 从 16.565 ms 降到 7.588 ms。与此同时 Draw run 从 2
增到 3，bridge 从 4 次 / 29,491,200 bytes 增到 6 次 / 44,236,800 bytes，mixed preflight median
从 6.632 ms 增到 10.091 ms；这些单轮短测只证明候选值得检查，不构成收益签收。

几何门禁通过：frame 30 对 144,630 个 dynamic 顶点做 device-local differential，position、normal、UV
均为零 mismatch。最终画面门禁失败：同一 near 60 固定 tick 截图中，infected 的面光照和轮廓明暗明显
改变。旧 infected CPU lowering 在 material 粒度执行专用纯色与逐面 Q8 form-light 语义，直接复用通用
skinned Draw 的 material/form-light 合同不能保持该结果。由于正确性先于耗时，未进入双方各五轮低扰动
采样，实验代码及临时 CLI 已全部撤销。

证据保存在 `tmp/rb2-infected-short-20260922/`、`tmp/rb2-infected-short4-20260922/` 与
`tmp/rb2-infected-diff-audit-20260922/`。下一 producer 候选回到 gear/weapon，但须等待 M2 后重新归因；
进入实现前还必须证明它能与已有 AI body Draw 合并并保持 socket、受击/动作和材质语义，不能由本次
infected 的短测数字推导其收益。

## 复核入口与验证边界

```powershell
powershell -NoProfile -File tools/gpu_metrics_test.ps1 -LogPath tmp/rb2-rigid-baseline-audit-20260922/round-01-near-0.runtime.log
powershell -NoProfile -File tools/gpu_rb2_candidate_report.ps1 -AuditDirectory tmp/rb2-candidate-fixed-audit-20260922 -StatsDirectory tmp/rb2-candidate-fixed-noaudit-20260922 -OutputJson tmp/rb2-candidate-report-20260922.json
```

本轮已完成 Windows build/package、真实等待退出的 logic test、旧/新 audit 回放与错帧拒绝、
正常 skinning 四场景两组各两轮、CPU-skinning-off 零 submit 实机复核、Quick 8/8 和 `git diff --check`。
Quick 证据在 `tmp/rb2-audit-quick-20260922/`，涵盖 mixed native resize 和角色 vertex differential。
本轮不改变绘制、同步、presenter 或资源生命周期；未重跑 Full、validation/fault/soak 或第二物理 GPU。
受限迁移尚未实现，因此不存在迁移画面签收或净收益结论。
