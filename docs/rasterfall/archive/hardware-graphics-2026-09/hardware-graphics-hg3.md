# Hardware Graphics HG-3：普通 opaque static RMESH 扩围

> 文档更新：2026-09-20
> 源码核对基线：当前工作区；Windows Intel 最终 baseline `tmp/hg3-20260920-final2` 已通过 differential、near/mid、Fog、strict-native、建筑内部/远处薄结构 native capture 与 320 帧 Campaign 门禁。连续 Draw 间的空 ordering flush 已消除，P0 A/B 证明稳定净收益，HG-3 完成。

## 当前结论

HG-3 原计划分为少量资产试点（HG-3A）和扩大普通 opaque static RMESH（HG-3B）。实际代码演进中，
HG-2B 为验证正常混合帧，已经把所有满足 Draw V0 合同的普通 static RMESH 接入 hardware Draw；因此
HG-3 不再人为把已工作的资产退回小 allowlist。当前资格仍由材质、透明度、变形、scope、range 和数值
门禁决定，`boundary_wall` 保持程序化 RasterCmd，透明或特殊材质整实例留在 legacy producer。

本 checkpoint 补齐此前缺少的可审计退出条件：

- `draw_triangles`：本帧被 Draw consumer 接收的 static RMESH 源三角形；
- `cpu_lowered_triangles`：同步 reference 实际下沉到 CPU triangle frontend 的数量；strict native 必须为零；
- `legacy_instances` / `legacy_triangles`：资格拒绝后回到旧 RMESH producer 的实例和源三角形；
- `draw_asset_mask` / `legacy_asset_mask`：按 `rasterfall_prop_asset_id` 位编号记录本帧可见资产族；
- `static_cmd` 仍是 static producer 产生的 legacy RasterCmd 数量，包含明确保留的程序化 boundary wall，
  不能要求归零。

资产 mask 只覆盖 RMESH。bit N 对应 asset ID N；ID 36 的 `boundary_wall` 不进入 mask。

## 当前实机证据

Windows Intel strict-native 30 帧固定 near/0 smoke：

- 30/30 `path=gpu-native`，零 readback、CPU framebuffer copy、fallback 和 hot queue-idle；
- 稳态 `instances=15`、`items=57`、`draw_triangles=5792`；
- `cpu_lowered_triangles=0`、`legacy_instances=0`、`legacy_triangles=0`；
- `draw_asset_mask=0x3dce`，对应当前视野内 crate、barrier、lamp_post、vent_unit、workbench、
  ammo_container、pipe_module、power_unit、gate_frame、control_cabinet；
- `legacy_asset_mask=0`；首次资源上传后稳态 `gpu_upload_bytes=0`。

修正 baseline 脚本此前固定场景仅运行 46 帧的问题后，完整 Intel baseline
`tmp/hg3-20260920-acceptance-120b` 通过：differential suite、`--logic-test`、
near/mid stream reference、CPU 对照、strict-native Fog 开关和 320 帧正式 Campaign 均正常退出。
所有 strict-native 测量帧均为非零 Draw triangles/asset mask、零 CPU lowering、零 legacy RMESH、
零稳态 VB/IB/texture 上传、零 fallback/readback/CPU framebuffer copy。Campaign 为 `world=1`，
实际进入波次并观察到活敌。

诊断性 GPU 最终帧另在自动移动/旋转的第 30、90、180、270 帧抓取。最终 baseline 又新增
`--gpu-normal-scene interior 0` 与 `thin-far 0` 两个真实 Campaign normal-world 相机，并直接抓取
strict native 最终帧 `native-interior.bmp`、`native-thin-far.bmp`。前者位于西侧维修建筑内部，近距离
观察墙面反面、梁和线槽轮廓；后者从约 12.5k RFU 外观察门框、线槽、管线和设备轮廓。两图均为
30/30 native、零 readback/CPU framebuffer copy，未见漏画、重复绘制、反面穿透或远处薄结构断裂。
近面保守数值门禁分别让 4/2 个实例留在 GPU compute RasterCmd；它们不是 CPU lowering 或 fallback。

### P0 A/B 结论

丢弃前 16 帧后，固定场景数据如下。CPU 与 strict mixed 的阶段归属不同：strict 路径为保持
Raster/Draw 原始顺序，会在 static 序列内消费 legacy Raster 段，因此 `static_ms` 不能解释为纯 Draw
编码耗时；whole-loop 才是较接近的端到端对照。

| 场景 | CPU static ms / static_cmd | strict static ms / static_cmd | CPU whole median/P95 | strict whole median/P95 | strict GPU Raster median/P95 | bridge 中位数 | Draw 中位数 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| near | 2.967 / 5184 | 10.044 / 1794 | 28.054 / 30.691 ms | 30.609 / 35.437 ms | 12.246 / 38.451 ms | 1.015 ms | 0.344 ms |
| mid | 3.241 / 5660 | 12.104 / 1986 | 37.588 / 41.868 ms | 35.879 / 60.861 ms | 16.820 / 36.341 ms | 0.729 ms | 0.240 ms |

上述数据定位到 strict static producer 对每个连续 Draw 实例都调用 `toy_renderer_flush()`；实际只有
首个 Draw 或插入 RasterCmd 后才需要建立顺序边界。空 flush 的 watchdog/统计 bookkeeping 在 near
中位数累计约 8.8 ms，而 Draw plan 提交仅约 0.02 ms。现在仅当 `cmd_count>0` 时 flush，顺序合同不变。

最终同批次 A/B（丢弃前 16 帧）为：near CPU/strict whole-loop 中位数 27.813/22.488 ms、P95
29.819/28.042 ms，static 2.956/1.554 ms；mid whole-loop 38.326/25.690 ms、P95 43.865/28.174 ms，
static 3.237/1.681 ms。strict GPU Raster 中位数 near/mid 为 18.158/17.008 ms，Draw 为
0.466/0.257 ms。正式 Campaign 320 帧 whole-loop 中位数/P95 为 30.526/42.914 ms，scene 中位数
7.493 ms。性能净收益与命令迁移现已同时满足。

## 验收与剩余工作

从仓库根运行完整基线，并显式标记 HG-3：

```powershell
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_baseline.ps1 `
  -Checkpoint HG-3 -OutputDirectory tmp/hg3-<timestamp>
```

脚本对每个 strict-native 帧强制检查：`draw_triangles>0`、`cpu_lowered_triangles=0`、
`legacy_instances=0`、`legacy_triangles=0`、非零 Draw asset mask 和零 legacy asset mask。CPU 对照仍允许
同步 reference lowering。`interior`/`thin-far` 专项允许 producer 的保守 numeric gate 把近面或极端投影
实例保留为 GPU compute RasterCmd，但仍强制零 CPU lowering、零 readback/copy、零非 numeric 拒绝。
最终自动门禁与人工图像检查均已通过。

Linux 和其他 GPU 仍未验收；这不阻塞按既定 Windows Intel checkpoint 口径签收 HG-3。
