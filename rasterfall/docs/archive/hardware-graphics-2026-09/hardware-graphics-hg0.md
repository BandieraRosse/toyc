# HG-0 checkpoint：事实与架构冻结

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 下述 texture full-scan 失败已由 HG-1A 前置修复定位并处理；原 HG-0 结果保留，后续证据见 [修复记录](hardware-graphics-hg1-preflight.md)。
> 源码核对基线：`e036b809ed28a17f0151d241e8b34b04794f067e` + HG-0 改动；Windows MinGW package、Intel Iris Xe 实测。

状态：HG-0 完成。接口和数值合同见 [架构文档](hardware-graphics-architecture.md)，后续顺序见
[根计划](../../GPU%20hardware.md)。本 checkpoint 没有实现 Draw IR 或硬件 graphics pipeline。

## 交付与可复核证据

- `hardware-graphics-architecture.md`：producer/ownership 表、Draw V0 草案、资源生命周期、数值语义、混合 target/depth 门禁和场景矩阵。
- `tools/hardware_graphics_baseline.ps1`：新目录保存原始命令、退出码、SHA256、GPU/driver、CPU/compute captures、逐帧审计与耗时统计。
- 显式 `--frame-audit` 从慢帧抽样改为逐帧记录，避免遗漏快帧及统计偏差；关闭审计的行为不变。
- 独立 `rf-gpu-raster-diff-test --capture-success` 必须配合 replay；保存成功的 color/depth/report。
  同时移除原先重复执行同一 replay 的分支。该诊断不进入游戏二进制。

最终测量命令：

```powershell
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_baseline.ps1 -OutputDirectory tmp/hg0-accepted
```

退出码 0，manifest.result=PASS。所有生成物保留在本地 `tmp/hg0-accepted/`，不提交图片、二进制、私有资产或缓存。
构建日志为 `tmp/hg0-build-2.log`。package exe SHA256：
`21459868F2ACC609BC81F79B9246C5A063F0CD923F7DD7075A43B52E80086C0B`。
实际 adapter 为 Intel(R) Iris(R) Xe Graphics；driver `32.0.101.6314`（display-class registry；CIM 拒绝访问）。
完整资产清单/hash、测试工具 hash、工作区状态在 manifest，不在此复制私有资源路径。

## 验收结果

Windows package 构建、实际 `--help`、`--logic-test` 通过。固定 normal-frame audit 的实际结果：
1280×720，camera=(-13000,-12000)，direction=(0,1024)，pitch=(0,1024)；总命令 20321，
floor/map/static 分别 3594/1527/8957，gallery=98，later=6145，其他 scene 分项为零。
这组总数只属于离屏 audit，不是 normal native retained 分母。

| Selected stream | 命令数 | CPU/GPU color hash | CPU/GPU depth hash | color/depth mismatch |
| --- | ---: | --- | --- | --- |
| near/0 | 15902 | `8168a2d7052fbad0` | `00525956d6a3b247` | 0 / 0 |
| near/30 | 24061 | `477b69de9354f2b1` | `5bbc86552bd907f3` | 0 / 0 |
| mid/0 | 15121 | `e57252d610310a73` | `2341de43d9e2ac90` | 0 / 0 |
| mid/30 | 26278 | `e57252d610310a73` | `2341de43d9e2ac90` | 0 / 0 |

报告中的 hash 为 differential 内置 FNV 风格 hash；不是 manifest 的 SHA256。
mid/30 命令数增加但输出 hash 与 mid/0 相同，不能据此宣称敌人可见；此处验收的是相同 stream 的 CPU/compute 等价。
真实波次另由 normal frame 门禁覆盖。

| 正常运行（CLI 场景名） | 帧数 | 第 17–46 帧 GPU frontend 中位数 / P95 |
| --- | ---: | ---: |
| CPU near / mid | 各 46 | 不适用 |
| native near | 46 | 25.555 / 28.974 ms |
| native near + Fog | 46 | 26.505 / 30.236 ms |
| native mid | 46 | 25.333 / 29.990 ms |
| native mid + Fog | 46 | 25.868 / 32.514 ms |

以上 native/Fog 每帧均 gpu-native、fallback=0、readback=0、CPU framebuffer copy=0、非法层迁移=0。
near/mid 在 normal runtime 首帧后的实际位置都回到 Campaign spawn；表中名称是 CLI 标签，不是稳定距离对照。
真正两个距离的冻结证据是前述离屏 stream。计时包含逐帧审计环境的开销，只作为本机本轮基线，不是性能提升结论。

正式地图波次显式指定 `--map rasterfall/assets/maps/rasterfall.map`；320/320 帧通过相同 strict 门禁，
日志确认 world=1、phase 进入波次且 alive>0。省略地图的早期试运行进入 Outpost，已排除出正式地图验收。

## 已知失败与未覆盖项

额外运行独立 differential **全套 fixture 未通过**：texture full-scan 在 pixel 135 `(24,3)` 得到
CPU `ff07080a/500`，GPU `ff0b0f14/0`。以 HEAD 原始测试源码重新编译也复现同一失败；
日志分别为 `tmp/hg0-diff-suite.log`、`tmp/hg0-diff-original.log`。这不是本次成功导出开关引入的差异。
HG-0 完成的是基线冻结，不宣称当时所有 compute fixture 通过。后续 HG-1A 已定位到 CPU planar
vertex-lit 忽略 alpha/no-depth-write，修复恒定和插值光照路径；没有放宽容差或更新 expected 值。
后续记录不改写本 checkpoint 的历史失败事实。

本机仅覆盖 Windows/Intel；Linux freestanding、其他 GPU、resize/minimize/swapchain 重建没有在本次重新执行。
HG-2B 的 graphics/compute 混合深度、生命周期和 native present 门禁尚未实现或验收。
本次不新增编译单元、资产或 normal CLI 参数，未改 Linux/self/Windows 链接文件列表。
