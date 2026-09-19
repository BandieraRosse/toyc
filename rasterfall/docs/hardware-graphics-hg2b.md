# HG-2B：整数深度与 GPU target bridge

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 工作区新增 `rf_gpu_vulkan_raster_segment()`、compute CLEAR/LOAD 范围执行和 Intel 分段回归；实现与限制见下文。尚未连接 graphics bridge。
> 源码核对基线：`0d721581` 加本次工作区；`rf_gpu_graphics.h`、`rf_gpu_vulkan_graphics.inc`、`graphics_compat.vert/.frag`、`graphics_bridge.comp` 与独立 oracle；CPU 合同对照 `rasterfall_render.c` near clipping/project 和 `lib/graphics/renderer.c`；Windows Intel 实测。

HG-2B 进行中：整数深度前置阻塞已修复，GPU attachment/buffer 往返转换与 LOAD 续画已通过。
Raster ABI 已有独立分段执行基础；graphics 互操作、Core Draw/Raster 顺序和 strict native 门禁尚未实现，不能标记
HG-2B 完成或推进 normal-frame hardware props。正常游戏仍消费原 CPU/compute 路径。

## 入口与判定

沿用 HG-2A 构建，无新编译单元或玩家 CLI：

```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& C:\msys64\usr\bin\make.exe -f windows/Makefile gpu-graphics-test
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_proof.ps1 -DepthGate -OutputDirectory tmp/hg2b-new
```

直接入口为 `build-windows/rf-gpu-graphics-test.exe --depth-gate`。失败返回 1，wrapper 保存
manifest、实际适配器、驱动、exe hash、命令和日志 hash；失败不会转换成预期失败 PASS。
不带参数仍执行原 HG-2A proof；`-DepthGate` 执行整数深度和 target roundtrip 前置用例，
成功不代表完整 HG-2B 通过。shader 编译仍使用 `tools/generate_gpu_graphics_spirv.py`；
新增 shader 已加入根/Windows Makefile 依赖，构建使用检入 SPIR-V，无新玩家资源或 freestanding 编译单元。

## 状态所有者与数值合同

`rf_gpu_graphics` 仍是 hosted 诊断 owner，复用 backend device 与 graphics/compute queue，
单帧在途、同步 fence 完成。`rf_gpu_graphics_draw.integer_depth=1` 选择新的兼容 pipeline；
0 保留原 HG-2A shader 作为对照。当前要求 device 已启用 `shaderInt64`，否则创建失败。

- CPU 在资源上传时生成每源三角形六个固定 index slots；原 vertex/source index 保持持久
  storage buffer。每帧仍每 submesh 一个 indexed draw，未在 CPU 每帧裁剪或展开三角形。
- `graphics_compat.vert` 按源索引读取三个角，执行原整数 yaw/scale/pivot/camera，随后按
  near=64 裁剪 position/UV；交点有符号除法向零截断。四角 polygon 输出两个 triangle，
  三角 polygon 的第二个 triangle 退化，完全不可见的 primitive 全部退化。
- clipped vertex 先整数投影，再形成 `1048576/z` 及 UV-over-Z。输出屏幕坐标加半像素，
  使 Vulkan 单采样中心对应 CPU 整数像素；参见 [Vulkan rasterization](https://docs.vulkan.org/spec/latest/chapters/primsrast.html)。
  全部投影角和 UV 项以 flat payload 保留，侧平面裁剪不改变深度平面。
- `graphics_compat.frag` 用 int64 edge weights 在整数像素上求 inverse-Z，最后一次整数
  除法后映射为 D32 的 `inverse_z/16384`。texture 使用未归一化的 UV 加权和除 inverse-Z
  加权和，匹配 CPU 的截断顺序；保持 nearest/repeat 与原 form/scene Q8 顺序。
- D32 clear=0，硬件 `GREATER_OR_EQUAL` 测试/写入；不依靠 float varying 的插值精度决定遮挡。
  CPU/compute 仍有自己的边缘包含规则，因此仅固定一像素边缘带允许覆盖差异，内部深度和颜色要求精确相等。
- 为保证 edge×UV 加权和不溢出 int64，提交前用上传时保存的 mesh bounds 做 O(1) 保守检查，
  只接受投影范围落入 `[-16384,16384]` 的 draw。该检查可能拒绝实际可见范围更小的 mesh；
  这是显式 hosted proof 限制，不是静默裁掉几何。还未完成 normal-prop 范围评估。

## Target 与同步合同

| 目标 | 格式/编码 | owner 与有效期 |
| --- | --- | --- |
| graphics attachments | RGBA8_UNORM / D32_SFLOAT，深度为量化 inverse-Z / 16384 | `rf_graphics_target`，成功 draw 后有效；resize 清除有效性 |
| transfer buffer | 连续 RGBA8 words，随后 D32 float bits | 同 target，device-local；仅连接 image copies 与 bridge shader |
| raster buffer | 连续 `0xffRRGGBB` words，随后 signed-compatible nonnegative inverse-Z words | 同 target，device-local；当前只供独立 bridge proof，尚未绑定 normal Raster ABI consumer |
| readback buffer | 按明确诊断入口解释布局 | host-visible，仅验收输出，不进入续画/import 输入 |

`rf_gpu_graphics_render()` 使用 attachment CLEAR。`rf_gpu_graphics_continue()` 先原子检查
全部 draw、extent 和已有内容，再执行以下链路：

```text
graphics color/depth (TRANSFER_SRC)
  → GPU image-to-buffer copy
  → graphics_bridge.comp export（RGBA→ARGB、D32→整数 inverse-Z）
  → graphics_bridge.comp import（ARGB→RGBA、整数 inverse-Z→D32）
  → GPU buffer-to-image copy (TRANSFER_DST)
  → attachment LOAD + later indexed draws
  → diagnostic readback
```

bridge 明确做数值转换，不能把 D32 bits 当作整数 inverse-Z。先后的 compute dispatch
之间有 shader read/write barrier，image copy 与 compute/attachment 之间有 transfer、
shader、early/late-depth、color-attachment 访问依赖和 image layout transition。
重复 bridge 的 buffer 复用也有依赖。resize 只重建目标，mesh/texture 不重传。
无效续画、越界 draw、resize 后首次 LOAD 均在提交前拒绝；提交故障使 owner poisoned。

`rf_gpu_graphics_read_bridge()` 仅诊断最近一次 export 的整数编码，不能用于上传 framebuffer。
`bridge_roundtrips` 记录实际 GPU 往返次数；`bridge_transfer_bytes` 仅统计 image↔buffer copy
字节，每像素每往返 16 bytes，不包含 conversion shader 的内部读写或诊断 readback。
目前未给出 GPU 时间与性能收益；VS 重复三角形准备和 fragment int64 成本需后续单独评估。

## 证据与覆盖边界

最初失败证据保留在 `tmp/hg2b-depth-evidence/manifest.json` 与 `proof.log`，Intel Iris Xe，驱动
32.0.101.6314。常深度对照无误差；near-crossing 内部深度最大误差 384，camera-plane-corner
最大误差 5000；behind-camera-corner 的被测内部像素没有有效正深度。前置门禁退出码为 1。
原 HG-2A proof 重新运行通过，日志为 `tmp/hg2b-depth/hg2a-regression.log`。

原 HG-2A VS 在裁剪前用 `1048576/max(z,1)` 生成 noperspective varying，
固定功能裁剪不会重新执行 CPU 的整数交点、投影与 inverse-Z 量化；z<=0 的钳制还会
引入极大 varying。FS 的 floor/clamp 不能补回上游差异。GPU 半像素采样与 CPU 整数采样
也是合同差异，不能仅靠放宽边缘带或全图容差消除。新 pipeline 明确实现这些整数步骤，
原 pipeline 保留对照，未被错误地标记为满足 CPU 深度合同。

当前证据：

- `tmp/hg2b-depth-bridge-verified/manifest.json` 为 PASS；原 HG-2A 回归为
  `tmp/hg2b-old-proof-verified/manifest.json`。新增 `compat_oracle()` 独立计算整数裁剪/投影/
  纹理/深度；近面、相机平面、相机后方、两角裁剪、恰好在近面与退化图元、斜面、变换、
  单双面、同深度覆盖、远处薄墙、奇数 extent 与 resize 的内部颜色和深度精确一致。
- roundtrip 后的 LOAD 与一次性 graphics Draw 序列要求全图 color/depth 字节一致，
  不排除边缘；同时单独读出 compute 编码核对，避免 export/import 对称错误相互抵消。
  用例在续画前破坏 host 输出数组，确认它不作为 import 输入；重复往返和 resize 不重传 mesh/texture。
- `tmp/hg2b-validation-final.log` / `tmp/hg2b-validation-loader-final.log` 确认加载 Khronos validation，
  启用 `VK_VALIDATION_VALIDATE_SYNC=1`，未报告 VUID 或同步 hazard。新 shader 通过
  `spirv-val --target-env vulkan1.0`。
- `tmp/hg2b-package-build.log` 为 Windows package 构建记录，
  `tmp/hg2b-normal-regression/manifest.json` 为 PASS：原完整 differential、CLI/logic、
  selected world captures、strict native/Fog 与 Campaign 波次均通过。此 native 验收仍是旧 compute 路径。

以上是独立 oracle 和 attachment↔buffer bridge proof，**尚未验证真实 Raster ABI compute
前段/后段与 graphics 的交错遮挡**。bridge buffer 的布局也还不是 normal raster 两个 buffer
的绑定 adapter。Linux、其他 GPU、设备恢复、混合 native present 均未验证。

## 后续实施顺序

1. 将已实现的 compute CLEAR / LOAD_EXISTING 分段基础接入当前 graphics 桥接目标，验证真实双向遮挡。
2. Core 冻结 Draw/Raster spans，保持 WORLD partition 后稳定顺序，检查整帧资格后再提交；
   补 transparent 后段、独立 VIEWMODEL depth/coverage、一次 Post/overlay。
3. 完成 strict hardware-required 的 unexpected lowering/readback/copy 门禁、native present
   与 resize/swapchain 重建。全部通过后再进入 HG-3A normal-prop allowlist。

## Raster ABI 分段基础（HG-2B 进行中）

`gpu/include/rf_gpu_vulkan_backend.h` 的 `rf_gpu_vulkan_raster_segment()` 是 hosted
诊断接口，复用原 raster owner 的 device-local color/depth。每次提交仍验证完整 Raster ABI
stream 和全部纹理描述；push constants 单独携带 `[first,end)` 与 CLEAR/LOAD_EXISTING，
不修改 ABI header、endian tag 或命令编码。分桶与 full-scan shader 均只执行所选范围。

- CLEAR 必须从 command 0 开始且包含原 clear/sky 和 clear-depth。LOAD 必须有此前成功的
  非末段内容，且不能重新执行这两条起始命令；新建/resize target 与已结束的帧均拒绝 LOAD。
- 非末段不做 Post、overlay、present、GPU→host copy 或 map；后续 LOAD 从 GPU buffer
  读取已有 color/inverse-Z。段间 barrier 覆盖 compute 读写与此前 transfer 读取。
- VIEWMODEL marker 与完整后缀只能位于末段，避免跨 dispatch 丢失独立 viewmodel depth；
  WORLD depth 保持独立，末段生成 coverage 后只执行一次 Post。当前末段输出是诊断 readback。
- 参数/完整 stream/纹理 preflight 拒绝保留此前有效目标；执行失败使 continuation 无效。
  调用者负责冻结帧及安排范围顺序，本接口尚不替代 Core 整帧计划或整帧原子 preflight。
- full-scan 未分配 tile buffers 时，未使用的 descriptor 绑定有效 buffer，避免写入空句柄；
  validation 层发现并验证了此修复。

构建与复核：`make -f windows/Makefile gpu-raster-test` 后运行
`build-windows/rf-gpu-raster-test.exe`；Linux 原 `make gpu-raster-test` 使用同一测试源。
无新增编译单元、玩家 CLI 或资源；检入的四种 raster SPIR-V 已重新生成并通过
`spirv-val --target-env vulkan1.0`。

Intel 实测记录在 `tmp/hg2b-segments-validation-clean.log`：8/16 工作组、分桶/full-scan、
Fog 开关、奇数 extent/非紧密 stride、远处遮挡、同深度、透明后段和独立 VIEWMODEL
与一次性执行的 color/depth 精确一致。用例修改已消费的前段命令后再 LOAD，以防整帧重放
产生假阳性；覆盖未初始化/结束后 LOAD、空段、非法范围、非法 VIEWMODEL 切点与范围外坏命令。
日志确认 Khronos validation 实际加载，开启 `VK_VALIDATION_VALIDATE_SYNC=1`，无 VUID/hazard。
正常路径完整回归记录在 `tmp/hg2b-segments-normal/manifest.json`：differential、CLI/logic、
固定视角 captures、strict native/Fog 与 Campaign 波次通过。随后修复的 full-scan 空 descriptor
由上述 validation 用例覆盖，最终 differential 记录为
`tmp/hg2b-segments-differential-final.log`，Windows package 构建记录为
`tmp/hg2b-segments-package-final.log`。最终 package 的 strict native smoke 记录为
`tmp/hg2b-segments-native-final.log`，退出码 0，readback/CPU copy 为零。
这些 native 结果仍属于原 compute 路径。

目前每段仍重新上传、验证和分桶完整 stream，属于正确性基础而非性能交付。没有 graphics
buffer adapter、Core mixed spans 或混合 native present；这些仍是 HG-2B 下一步，正常
producer 不进入 hardware 路径。Linux 与其他 GPU 尚未验证本次改动。
