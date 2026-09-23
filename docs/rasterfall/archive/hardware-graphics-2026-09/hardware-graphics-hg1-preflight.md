# HG-1A 前置修复：CPU/compute 透明顶点光照等价

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)

> 文档更新：2026-09-19
> 源码核对基线：`37b47c0` 加本次工作区；`lib/graphics/renderer.c`、`gpu/src/rf_gpu_raster_diff_test.c`、`tools/hardware_graphics_baseline.ps1`。

本记录处理 [HG-0](hardware-graphics-hg0.md) 明确要求 HG-1A 关闭前定位的 texture full-scan 失败。
Draw/reference vertical slice 和资源 registry 尚未实现，不将本次修复计作 HG-1A 完成。

## 根因与修改边界

texture fixture 的最后一个命令实际是带 alpha=128 的 vertex-lit triangle。原 CPU 在像素
`(24,3)` 写入未混合的 `ff07080a` 与 depth=500；GPU 正确执行 source-over，得到
`ff0b0f14` 并保持 depth=0。因此失败名称虽含 texture，问题不在纹理上传或 shader。

`raster_planar_vertex_lit()` 原先无条件写 depth/color，忽略命令的材质透明度；
`raster_command()` 中三个顶点光照相同时走 `raster_flat()`，也把 alpha/no-depth-write 硬编码为 255/0。
两个分支现均消费已冻结的命令字段，与 flat/textured 的透明合同一致：

- alpha=0 不改 color、depth、coverage。
- 部分透明先按原整数光照/Fog 求色，再逐通道 source-over，保持原 depth。
- alpha=255 仍尊重显式 no-depth-write；opaque 仍按 `>=` 比较并写深度。
- 非零 alpha 的通过像素标记 coverage；保留既有 worker 分类与混合计数口径。

未修改 GPU shader、Raster ABI、排序、玩法状态或编译单元列表，也没有引入宿主 libc 依赖。

## 回归门禁

独立 differential 新增恒定/插值光照 × alpha=0/128/255 source-over 与 opaque 用例。
在顶点 A 令 light=128，源色 `0x804020` 必须变为 `0x402010`；在底色 `0x204060`
上以 alpha=128 混合必须得到 `0x302f37`。先断言软件 renderer 的固定颜色、depth、coverage，
再断言 CPU reference，并执行 full-scan/tile-binned 全像素 color/depth 比较。
这些固定预期值不从 GPU 输出生成。

用 HEAD 原始 `renderer.c` 编译新测试，退出码为 1，首个 alpha=0 固定颜色断言失败；
修复后完整 differential 通过。前后日志分别为 `tmp/hg1-preflight-before.log` 与
`tmp/hg1-preflight-diff.log`，原始临时源码为 `tmp/hg1-renderer-before.c`。

基线脚本现在先执行完整 differential suite，失败即终止，不能仅以 selected world replay
通过掩盖独立 fixture 失败。differential 从仓库根执行以保存 `build/` 的 replay self-check，
游戏继续从 package 执行。`-Checkpoint` 仅标注证据，不表示 checkpoint 自动完成。

## Windows 验证入口

```powershell
powershell -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 package
# 按架构文档的 MinGW 命令重新构建 build-windows/rf-gpu-raster-diff-test.exe
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_baseline.ps1 -OutputDirectory tmp/hg1-preflight-accepted -Checkpoint HG-1A-preflight
```

构建日志：`tmp/hg1-preflight-build.log`；基线日志：`tmp/hg1-preflight-baseline.log`；
可执行文件与资产 SHA256、GPU/driver、原始命令/退出码、captured color/depth 与逐帧审计保存在
`tmp/hg1-preflight-accepted/`。生成物不提交。

本次脚本退出码 0，manifest.result=PASS；实际适配器为 Intel Iris Xe。验收结果：

| 门禁 | 结果 |
| --- | --- |
| Windows package、`--help`、`--logic-test` | 通过 |
| 完整 differential，含新固定值门禁与原 texture fixture | CPU/full-scan/tile-binned color/depth 零差异 |
| near/mid × 0/30 selected world stream | 四组 CPU/compute color/depth 零差异，保存 BMP/depth/report |
| CPU near/mid | 各 46 帧通过 |
| strict native near/mid × Fog 开/关 | 四组各 46 帧，无 fallback/readback/CPU copy/非法层迁移 |
| 显式 Campaign 地图真实波次 | 320 帧同样通过 strict 门禁；world=1 且出现活敌 |

normal near/mid 仍只控制初始相机，稳定位置口径沿用 HG-0 限制；这不是性能提升或两个稳定距离的结论。
此次完成的是 HG-1A 前置精确回归修复，下一步仍为单 prop Draw/reference vertical slice。

本轮未验证 Linux freestanding、其他 GPU、窗口 resize/minimize/swapchain 重建。
HG-2B graphics/compute 混合目标和深度门禁仍未实现。
