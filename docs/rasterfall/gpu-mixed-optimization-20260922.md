# Mixed 路径优化执行计划

> 文档更新：2026-09-22
> 源码核对基线：`2c318a1` metrics schema 6 与 RTX 3050 M2 preflight 三轮审计

## 目标与执行顺序

保留当前 producer、光照、color/depth、层序与 required native 合同，逐项减少 mixed 路径的重复工作。
Windows 原生 RTX 3050 现为性能开发与 A/B 主设备，Intel Iris Xe 为普通正确性和性能下限设备；具体目标、
冻结基线和采样口径见 [GPU 性能标准与冻结基线](gpu-performance-standards.md)。各阶段独立 ablation，
不把多个改动的收益混为一个结论；用户已有工作区修改保留。

| 阶段 | 实现与所有者 | 进入下一步的依据 |
| --- | --- | --- |
| M1：Raster segment 遍历 | `gpu/shaders/raster_v1.comp` 利用 `rf_gpu_raster_bin.c` 产生的有序 tile indices；先测上界提前终止，再评估 segment 起点定位 | color/depth differential、mixed/native 门禁通过；固定 workload A/B 显示是否值得保留 |
| M2：动态资源复用 | `rf_gpu_mixed_executor.c` 与 `rf_gpu_vulkan_graphics.inc` 按 frame slot 复用容量、CPU staging 和 descriptor，先保留同步模型 | 拆分分配、上传、skinning submit/wait 计时；证明 preflight 成本及生命周期合同 |
| M3：skinning 同步收敛 | 整帧预检后，将上传与 skinning 录入帧命令，在 Draw 消费前建立依赖 | M2 稳定；Full、resize、validation/sync、fault、soak；对比 whole-loop 与等待转移 |
| M4：depth bridge 去中间复制 | bridge 转换直接读写 Raster depth buffer，保留 D32 映射和 image copy | 独立 baseline；精确 depth differential、流量重新核算及完整同步/生命周期门禁 |
| M5：producer 后续候选 | infected 先补 material/tint/form/scene/clamp/整数舍入合同；gear/weapon 先做实际边界清单 | 隔离画面等价通过后才接入 mixed；不能用顶点 differential 代替最终画面，不能由端点标签推断 run 消失 |

M1 已按原 Intel 单设备范围签收。M2 起以 RTX 3050 为性能主线、Intel 为普通标准复核；M2–M5 为后续
独立阶段，具体实现由前序测量和合同检查收敛，不能一次性混入。

## M1 验证和证据

1. 从当前工作区构建并保存 baseline exe；记录 package hash 与既有修改。
2. 先做上界提前终止，生成 8×8/16×16 buffer/image shader；full-scan 保留为独立 reference。
3. 跑 Raster 分段和 mixed color/depth differential、Windows package 与 Quick；相关分段边界补回归。
4. baseline/candidate 串行短测 near 0/30/60 与 Campaign，audit 验证 workload、run、bridge 未变。
5. 候选有收益后，双方各五轮交替低扰动采样，报告 whole-loop、GPU Raster、P95/P99；最终 Full。
6. 更新本报告、总索引、架构和当前计划；原始日志、exe、截图只保存在 `tmp/`。

收益门禁不以单轮 median 或命令数判断。固定 workload 的命令内容、run、bridge 与输出画面应保持不变；
测试中的捕获 readback 只属于诊断，正常采样必须保持零 readback/fallback/CPU framebuffer copy。

## 执行状态

M1 已实现上界提前终止和非初始 segment 的 lower_bound。增加 sparse-segment 回归，覆盖 tile 内
command ID 间隙、无本段命令的 tile，以及 8×8/16×16、binned/full-scan 对照。
原有 raw 分段、mixed color/depth/stride differential 与 Windows Quick 均通过。首组正式五轮 A/B
显示稳定的 GPU Raster 收益；near 30 的独立 AC 五轮复测未复现首组 P99 大幅回退。重建后 Windows
Full 31/31 通过，因此保留上界终止与起点定位实现，M1 按 Intel 单设备范围签收。

## M1 过程证据

本轮生成物位于 `tmp/mixed-opt-20260922/`。三个 exe SHA-256：

- baseline：`FB5DD1F5CE53C94CE0BF84684F4B7E420C6D3553A222CBE5277FF27123461851`
- 仅上界提前终止：`3CE1B4F3DB063C1BE241A8896B6A4CF34EE3966492469217B039C2E8BD65C43B`
- 上界终止 + 起点定位：`1DE69DC073CBCB9E3DEF41D9D6D907350D7274A2C9E1F5CF8745B345016313B9`

短测只用于选择候选，不是最终收益签收。四场景的 GPU Raster median（ms）如下：

| 场景 | baseline | 仅上界终止 | 加起点定位 |
| --- | ---: | ---: | ---: |
| near 0 | 4.769 | 3.770 | 3.568 |
| near 30 | 13.652 | 10.558 | 9.537 |
| near 60 | 17.152 | 13.958 | 13.038 |
| Campaign | 12.762 | 8.954 | 8.395 |

`baseline-audit/` 与 `bounds-audit/` 的四场景规范化 workload sequence hash 完全一致；near bridge
仍为 4 次、29,491,200 bytes，Campaign 为 10 次、73,728,000 bytes。producer、Draw run、命令范围和
target 合同没有借优化改变。GPU 统计仍按完成的 frame ID 归属，不与 audit 墙钟混作低扰动收益。

`capture/` 的固定 tick/frame 30 near 60 首次 A/B BMP 完全相同；复测 near 60 有 27 个 RGB 像素差异，
范围 `(401,10)..(406,18)`，mid 0 有 176 个差异，范围 `(386,6)..(400,25)`，均为 HUD 的 FPS 数字。
已核对 `rasterfall_hud.c:render_wave_hud()` 与 runtime 的墙钟 FPS 更新；其余世界及 HUD 像素一致，
没有对图像做修改。当前 `--gpu-frame-capture` 只接受 `--gpu-normal-scene`，Campaign capture 请求被
参数解析拒绝，未计为通过；Campaign 由正常帧 audit、Full 与底层 mixed differential 覆盖。

一次实机门禁因用户查看游戏而拒绝并发启动；用户自行关闭后重跑通过，该次未进入性能采样。
Windows 字体/资产相对 exe 路径解析，单独存档的 exe 不能直接从 `tmp/` 运行；A/B 将两个 exe 放在同一
package 目录，以不同文件名串行运行，正式 `rasterfall.exe` 不替换。`gpu_rb0_sampling.ps1 -ExecutablePath`
显式检查这个约束。`tools/gpu_mixed_ablation.ps1` 每轮交替 AB/BA，并保留全部原始 manifest/stdout。

首组正式低扰动 A/B 位于 `ab-five/`，双方各五轮、四场景均采样成功，十个 manifest 均为 AC 供电，
且每个 variant 的 exe hash 跨轮一致。下表为五个单轮指标的中位数，不是把所有帧合并后重新计算的
分位数：

| 场景 | whole median 基线→候选 | whole P95 | whole P99 | GPU Raster median |
| --- | ---: | ---: | ---: | ---: |
| near 0 | 15.861→16.204 | 21.141→20.853 | 23.045→21.501 | 4.989→3.951 |
| near 30 | 22.872→21.289 | 26.919→28.443 | 30.736→50.007 | 13.586→9.733 |
| near 60 | 33.406→33.593 | 42.827→42.805 | 54.018→60.079 | 17.236→13.015 |
| Campaign | 21.359→17.450 | 23.578→20.422 | 27.141→23.680 | 12.768→8.491 |

因此当前只能确认 Campaign 有明显整帧收益、near 30 的整帧 median 改善，以及 near 60 的 GPU Raster
明显改善；不能宣称所有场景的整帧或长尾均改善。首组 near 30 的 P99 回退仍是 M1 签收阻塞项。

`near30-tail-five/` 尝试以每次 600 帧复核长尾，但只完成两对：第一对 baseline/candidate 的 whole
median 为 17.192/17.087 ms、P95 为 24.206/22.990 ms、P99 为 28.940/27.514 ms；第二对按相反顺序
运行，candidate/baseline 分别为 17.079/17.347 ms、22.215/24.399 ms、26.614/30.006 ms。
`03-baseline/` 没有 `manifest.json`，不得计入；该目录没有最终 `summary.json`。600 帧运行已经进入不同的
敌人/场景时段，整体负载明显低于原 120 帧高负载窗口，所以这两对不能覆盖首组 near 30 的 P99 问题。

后续同一 120 帧 workload 的五轮复测位于 `tmp/mixed-opt-near30-followup/`，但十次运行均为电池供电
（`ac_line_status=0`，平衡电源方案），与首组 AC 条件不一致。该组虽完整生成 `rounds.json` 与
`summary.json`，但 whole 与各 GPU phase 整体漂移，不能作为接受或拒绝 M1 的性能证据。该失败尝试暴露了
编排器只检查供电状态不变、没有要求 AC 的缺口；后续已补成启动前拒绝电池模式。

最终 AC 复测位于 `tmp/mixed-opt-near30-ac-final/`，双方各五轮均通过，exe hash 保持不变。五个单轮指标
中位数为：whole median `30.613→28.182 ms`（约 -7.9%），P95 `35.898→36.531 ms`（约 +1.8%），
P99 `41.805→41.776 ms`（基本持平），GPU Raster median `18.139→13.700 ms`（约 -24.5%）。因此首组
`30.736→50.007 ms` 的 near 30 P99 大幅回退没有复现；但 P95 也没有稳定改善，签收结论仅为 Raster
成本显著下降、whole median 改善且未发现可重复的 P99 回退，不宣称所有整帧长尾指标改善。

最终从当前工作区重新执行 `windows/NativeCodex.ps1 build`，随后 `tools/gpu_acceptance.ps1 -Full` 31/31
通过，证据目录为 `tmp/gpu-acceptance-20260922-194504/`。M1 没有改同步或资源生命周期，本轮未重复
RB-0 的 validation/sync、fault、soak；这些门禁在后续 M2/M3 涉及相应边界时必须重新执行。

`tools/gpu_mixed_ablation.ps1` 现默认在创建输出目录和启动首个样本前要求 AC，并在每个 manifest 后再次
检查 `ac_line_status=1`；电池拒绝路径已验证为非零退出且不创建输出目录。

## 后续执行计划

- 当前唯一未完成前置是补现有 RTX 3050 package 的 `-NoAudit` 五轮 baseline。完成前不开始 M2 候选实现，
  避免 baseline 与候选来自不同代码或 package。
- M2 计时已完成。实现按 frame slot 持久复用动态 resource、buffer 和 descriptor，容量只增长；稳态只
  上传当前有效数据，只有扩容、失败回滚或 executor teardown 才销毁。先保持当前 skinning submit/wait。
- M2 候选随后做五轮 AB/BA。审计必须新增或
  保留 build/reuse/grow/descriptor-update 计数，证明成本没有转移到 slot recycle 或 present。
- M3 仅在 M2 后 skinning fence 仍是最大固定成本时推进；若实施，必须补 Full、四 extent resize、validation/sync、
  fault 与 soak，明确等待从哪里转移到哪里。
- M4 depth bridge 保持独立 ablation，先建立精确 depth differential 和重新核算的 transfer 字节合同。
- M5 不恢复已撤销的 infected body 实验；infected 必须先补齐最终光照合同，下一 producer 候选仍先做
  gear/weapon 实际边界预检。
- 性能选择和 60 FPS 签收以 RTX 3050 为准；Intel 保留正确性、完整功能、普通 30 FPS 下限及不超过 10%
  可重复退化的复核，不要求两个设备达到相同帧率。

## M2 preflight 细分（AMD 5600H + RTX 3050）

同一新 package 在 Windows native、交流电、OEM 均衡方案下完成 near 0/30/60 与 Campaign 各三轮审计，
证据位于 `tmp/amd3050-m2-breakdown-20260922/`。四场景各自 workload sequence hash 跨轮一致，normal
路径保持零 fallback/readback/CPU framebuffer copy/hot queue-idle。`gpu_metrics.ps1` 升为 schema 6，
在原 preflight 下新增 dynamic release、target setup、dynamic CPU pack、dynamic resource create、plan build
和 Vulkan Raster preflight 六类计时。

轮间中位结果（ms）：

| 场景 | preflight | dynamic release | dynamic pack | dynamic resource | plan build | Raster preflight | texture measure | Raster pack | Draw encode | 子项未覆盖 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| near 0 | 20.482 | 5.591 | 0.872 | 10.881 | 0.218 | 0.514 | 0.036 | 0.294 | 0.974 | 1.100 |
| near 30 | 23.390 | 5.711 | 0.801 | 10.546 | 0.681 | 1.743 | 0.124 | 0.975 | 1.081 | 1.726 |
| near 60 | 24.516 | 5.150 | 0.885 | 10.314 | 1.106 | 2.802 | 0.214 | 1.576 | 0.952 | 1.515 |
| Campaign | 20.640 | 5.834 | 0.960 | 10.256 | 0.278 | 0.760 | 0.050 | 0.419 | 1.094 | 0.987 |

target setup 各场景中位均约 0.002 ms，未单列。子项合计已将 preflight 缺口压到约 1.0--1.7 ms。
动态资源创建包含当前每帧一次的 skinning submit/wait；对应四场景 skinning fence wait 中位约
5.236--5.488 ms。因此固定 CPU 动态输入打包不是主要成本，主要固定成本是 frame-slot 再次使用时销毁
上一资源约 5.2--5.8 ms，以及创建/上传/descriptor/skinning 整体约 10.3--10.9 ms。下一切片限定为
按 frame slot 复用动态 resource 与 descriptor，同时保持当前 skinning submit/wait 边界；先证明销毁/
重建消失且 workload、bridge、画面和生命周期门禁不变，再决定是否进入 M3。

本组 audit 的 whole-loop 数字及 package/hash 已冻结在
[GPU 性能标准与冻结基线](gpu-performance-standards.md)。audit 日志会扰动帧墙钟，因此不得把本组
whole-loop 直接当作 60 FPS 基线；正式性能基线与收益使用 `-NoAudit` 五轮 AB/BA。

## 后续阶段的重新归因

M1 候选 near 60 audit 的 median：CPU enemies 10.942 ms、AI teammates 3.841 ms、mixed preflight
7.242 ms；skinning fence wait 仅 0.295 ms，P95 0.384 ms，其余六类独立 graphics submit 为零。
这些是 audit 归因而非低扰动收益，且分位数不能相加。已有证据不支持把整个 preflight 归为 skinning
等待，也不支持优先进行 M3 的同步重构。M2 实施前应先补动态输入打包、资源分配/上传和 Raster binning
的互斥计时，再决定容量复用切片；不能用 `preflight - 若干 median` 代替真实 phase 测量。

## 复现入口

```powershell
# MSYS2 自带 Python 与原生 Windows glslang；不需要 WSL。
& C:/msys64/usr/bin/python3.exe tools/generate_gpu_raster_spirv.py /mingw64/bin/glslangValidator.exe
.\windows\NativeCodex.ps1 package
# A/B exe 须事先分别构建并存放在同一个 package 目录；此脚本不替换正式 exe。
powershell -NoProfile -File tools/gpu_mixed_ablation.ps1 `
  -BaselineExe build-windows/rasterfall-windows/rasterfall-baseline.exe `
  -CandidateExe build-windows/rasterfall-windows/rasterfall-bounds.exe `
  -Rounds 5 -OutputDirectory tmp/mixed-opt-ab-new
powershell -NoProfile -File tools/gpu_acceptance.ps1 -Full
```

Full 前须先重建 Windows hosted graphics/raster/differential、cache 和 mixed 测试 exe，避免只更新
package 而让独立测试继续使用旧 shader。新 Python generator 与现有 shell generator 使用相同数组名和
变体集合；未改动的 full-scan 生成物与基线无内容差异。
