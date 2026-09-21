# HG-1A：Static prop Draw/reference

> 文档更新：2026-09-19
> 后续源码核对：2026-09-19 [HG-1B](hardware-graphics-hg1b.md) 已替换正常 static prop 的借用资源适配器；下文保留 HG-1A checkpoint 当时的实现与证据。
> 源码核对基线：2026-09-19 工作区；`rasterfall_draw.h`、`render/rasterfall_draw_reference.inc`、`rasterfall_render.c`、`dev-tests/rasterfall_draw_reference_test.inc`、`rf_game_runtime.c`。

普通 opaque static RMESH 已按实例、submesh 提交 CPU-backed Draw V0，同步 reference
lowering 继续生成原 RasterCmd。CPU 和 compute 共用该路径；没有 hardware indexed draw、
GPU mesh cache、Core DrawSpan 或延迟资源引用。前置透明修复见 [原记录](hardware-graphics-hg1-preflight.md)。

## 所有权与接入

- `rasterfall_render_static_prop()` 保留 profile、asset、scale、yaw 和建筑强制剔除策略的解析。
  在顶点准备之前生成 `rasterfall_draw_view` / `rasterfall_draw_instance` 快照，并检查整实例资格。
- `rasterfall_draw.h` 定义 View、Instance、Material、Item 和拒绝原因。V0 固定 WORLD、triangle list、
  bottom pivot、alpha=255、nearest/repeat、primitive fog=0；顺序就是实例调用顺序和 primitive 顺序。
  mesh/texture 是同步借用的不可变 CPU backing，没有伪造 slot/generation。
- `render/rasterfall_draw_reference.inc` 的 resolver 按 submesh 解析 Draw，首次 consumer
  才准备该实例全部顶点，后续 submesh 复用缓存。producer 不遍历三角形，也不生成 screen vertex。
  整实例 preflight 会先扫描全部 submesh；任何一项不支持都在提交之前回到旧 producer。
- `lower_gallery_triangles()` 从旧 `render_gallery_model_range()` 原样提取整数循环；旧 producer
  和新 consumer 共用变换后的法线求光、near clipping、projection、材质求色和 RasterCmd helpers。
  每帧仍执行 CPU vertex/triangle lowering，不能将本阶段解释为减少 frontend 几何成本。
- consumer 显式应用已冻结的相机、Q10 facing、scale、scene/form light、材质颜色/纹理/单面策略及
  ambient/specular。退出时恢复调用者的 frontend/material/light 状态，仅保留缓存分配和诊断计时。
  尺寸变化后不能消费旧 view。延迟消费及 pinning 必须等 HG-1B。

`BOUNDARY_WALL` 仍提前进入程序化路径。透明 alpha/纹理、sphere/toon/edge、角色材质、骨骼/角色模型、
特殊调用 scope 和无效 submesh range 留在原路径。缺失或越界纹理按旧规则解析为无纹理颜色路径。
这是 HG-1A 的同步 reference 资格，不能作为 HG-3 hardware allowlist。

`--frame-audit` 新增 `draw-reference` 行：

| 字段 | 口径 |
| --- | --- |
| `instances` | 通过 preflight 的 RMESH 实例，包含随后整模型剔除的实例 |
| `items` | 整模型剔除之后提交的 submesh Draw 数 |
| `cpu_lowered_triangles` | 这些 Draw 的源 index-count / 3；不是裁剪后命令数或可见三角形数 |
| `legacy_instances`、`reject_*` | 未通过 preflight 的整实例及首个拒绝原因；不含 boundary 程序化入口 |

新增实现和测试是 `rasterfall_render.c` 的 `.inc`，没有新增编译单元。根 Makefile 的正常/self
依赖都已补齐；Windows 使用 `-MMD -MP` 自动追踪 header/inc。未引入宿主 libc 或新资源。

## 可复核验证

`--logic-test` 的 Draw/reference fixture 不依赖外部资产：同一合成 mesh 有两个 submesh、
每 submesh 两个三角形和相反 winding。覆盖 flat/nearest/missing texture、v9 ambient/specular、
v2 默认双面、强制单面、负向 yaw、非单位 scale、近面交叉、scene light，以及同帧两个重叠实例。
比较规范化命令的字段、纹理内容与顺序，忽略 padding/地址和无效 flat UV；随后比较完整 CPU color/depth。
还检查真实 near clipping/backface 计数、每 submesh 一个 Draw、透明/特殊材质/变形/range/scope 拒绝。
快照后修改外部 facing/light/rigid scope，验证 consumer 使用快照并恢复调用者状态。

Windows 构建入口为 `windows/NativeCodex.ps1 package`；基线入口为：

```powershell
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_baseline.ps1 -OutputDirectory tmp/hg1a-accepted -Checkpoint HG-1A
```

改动前证据在 `tmp/hg1a-before/`，首轮改动后在 `tmp/hg1a-after/`，最终验收在
`tmp/hg1a-accepted/`。manifest 保存实际进程退出码、exe/资产 SHA256、GPU/driver 和原始日志。
构建日志为 `tmp/hg1a-final-build.log`。生成物均不提交。

最终 manifest.result 为 PASS，各入口退出码均为 0；Draw fixture 输出 60 组、475 条规范化命令，
color/depth 零差异。与改动前的 33 份 BMP、stream、texture sidecar 和完整 depth 文件均逐文件
SHA256 一致，比较记录为 `tmp/hg1a-accepted-before-after.json`。四组 strict native/Fog 各 46 帧，
Campaign 波次 320 帧。一个稳定 normal-frame 审计为 113 个合格实例、147 个提交 Draw、12,824 个
源三角形、0 个 legacy 实例；实例统计包含模型剔除，不能用它与 Draw 数推导每模型 submesh 数。

已核对的门禁：

- Windows package、`--help`、完整 `--logic-test` 与新增 Draw fixture。
- 完整 CPU/compute differential；near/mid × 0/30 selected world stream 的完整 color/depth 零差异。
- 固定 normal-frame CPU BMP、四组 Raster ABI stream 与 texture sidecar，和改动前逐文件 SHA256 一致。
- Intel Iris Xe 的 CPU near/mid、strict native near/mid × Fog 开/关及显式 Campaign 真实波次。
  strict 运行没有 fallback、readback、CPU framebuffer copy 或非法层迁移。

normal near/mid 仍仅控制初始相机，稳定位置以审计为准；selected stream 不代表完整 normal frame。
没有主张性能提升。Linux freestanding、其他 GPU、resize/minimize/swapchain 重建本轮未验证。

下一 checkpoint 为 HG-1B：renderer registry、stable handle/generation、帧 pinning、卸载/重载与释放。
HG-2B 的 graphics/compute 目标、深度和顺序门禁通过之前，不接入 normal-frame hardware draw。
