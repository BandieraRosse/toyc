# Hardware Graphics：架构与 checkpoint

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 [HG-2B 真实交错桥接](hardware-graphics-hg2b.md#真实-raster-abi--graphics-交错桥接)：`rf_gpu_graphics_raster_draw()` 在同 device/extent 的未结束 Raster target 中插入整数 indexed draws，GPU 内双向传递 color/depth；`rf-gpu-raster-test --mixed-gate` 验证交错顺序。Core/native 混合接入仍待实现。
> 源码核对基线补充：2026-09-19 [HG-2B Raster ABI 分段基础](hardware-graphics-hg2b.md#raster-abi-分段基础hg-2b-进行中)：`rf_gpu_vulkan_raster_segment()` 使用独立范围/CLEAR/LOAD 参数，验证完整 stream；中间段不读回，VIEWMODEL/Post 留在末段。真实交错已由 `rf_gpu_graphics_raster_draw()` 接通；Core/native 接入仍待实现。
> 源码核对基线补充：2026-09-19 [HG-2B 整数深度与 target bridge](hardware-graphics-hg2b.md) 已实现 GPU 整数裁剪/投影/深度、GPU color/depth 往返转换及 attachment LOAD；Intel 前置门禁通过。Raster ABI CLEAR/LOAD 分段基础已在 Intel 验证；compute/graphics 桥接已通过 Intel 固定 fixture；Core 混合顺序与 strict native 门禁仍待实现，正常帧不变。
> 源码核对基线补充：2026-09-19 [HG-2A](hardware-graphics-hg2a.md) 独立 indexed draw proof 已通过 Intel 实机；新增 graphics executor 复用 backend device/queue，正常帧尚未消费它。
> 源码核对基线补充：2026-09-19 [HG-1B](hardware-graphics-hg1b.md) 已实现 static prop CPU bundle registry、stable handle/generation、Core 单帧 pin 与 world 退休/延迟释放。
> 源码核对基线补充：2026-09-19 [HG-1A Draw/reference](hardware-graphics-hg1a.md) 已实现普通 opaque static RMESH 同步 CPU-backed Draw；原 CPU/compute 精确回归通过，后续资源生命周期见 HG-1B。
> 源码核对基线：`e036b809` 加 HG-0 工作区；`rasterfall_render.c`、`rasterfall_render_frontend.h`、`rf_core_host.c`、`rf_game_runtime.c`、`rf_gpu_vulkan_backend.c`、`raster_v1.comp` 与 Windows package 实测。

本阶段执行根目录 [GPU hardware.md](../../GPU%20hardware.md) 的 HG-0 → HG-1A/1B → HG-2A/2B → HG-3 顺序。
HG-0 冻结事实、接口草案和诊断基线；HG-1A 已实现同步 CPU-backed Draw/reference。
HG-1B 已建立 CPU registry 与帧 pin；HG-2A 已建立独立离屏 graphics executor，延迟 Draw 消费与 Core DrawSpan 仍待实现。
当前 native 路径仍为 compute raster，不能将其称为 hardware indexed draw。

## 状态所有者与 producer 边界

| 层 | 当前所有者/入口 | 迁移责任 |
| --- | --- | --- |
| 权威玩法、world 选择 | `toy_game`、session、World Content | 不持有 GPU handle，不改变碰撞或地图语义 |
| 实例及资源选择 | `render_static_props()` → `rasterfall_render_static_prop()` | 已解析 profile、scale、yaw、world light 后提交每 submesh 一个 Draw；资格失败整实例保留旧 producer |
| 模型 CPU 定义 | `rasterfall_render_resources.h`、`render/rasterfall_render_resources.c` | HG-1B bundle registry 拥有 CPU mesh/material/texture；Game 使 world 资源退休，Core 完成帧 pin 后释放 |
| 顶点与三角形 frontend | `render/rasterfall_draw_reference.inc`、`prepare_gallery_vertex_cache()`、`lower_gallery_triangles()` | HG-1A 同步 reference；旧 gallery 与 Draw 共用整数循环，尚无跳过 lowering 的 hardware 路径 |
| 隐式模型状态 | `rasterfall_frontend_state` 及 renderer 文件级 lighting scopes | 提交时冻结，延迟 consumer 不重读 scope |
| RasterCmd | `toy_renderer`、`include/toy_renderer.h` | 保留 CPU 指针结构；不要与固定宽度 Raster ABI 混淆 |
| 层顺序、retained、fallback | `rf_core_host.c` | 持有 DrawSpan/RasterSpan 有序帧记录，整帧 preflight 后执行或 replay |
| Vulkan 资源与 present | `gpu/src/rf_gpu_vulkan_backend.c`、`rf_gpu_vulkan_graphics.inc` | normal compute buffer/Win32 transfer present；HG-2A 单 mesh graphics owner 持有 VB/IB/texels 和离屏 RGBA8/D32；HG-2B 已连接 Raster ABI 分段与整数兼容 attachment/buffer bridge；Core 混合帧/presenter 待接入 |
| 基线、验收 | `tools/hardware_graphics_baseline.ps1`、`rf_gpu_raster_diff_test.c` | 保留退出码、原始审计、stream、color/depth、环境标识 |

首个接点在 `rasterfall_render_static_prop()` 得到 model/profile 和实例策略之后、进入模型顶点准备之前。
`BOUNDARY_WALL` 在该函数早退到程序化盒体，继续留在 RasterCmd 路径。
建筑闭合资产 ARCH_BEAM 至 ARCH_FLOOR_HATCH 强制单面；不能因为旧 RFM2 缺少 sidedness 而丢弃该策略。
透明及不支持的材质先保留原路径。禁止在 `toy_renderer_triangle_*()` 外包装 Draw，那里已承担逐三角形成本。

## Draw V0 合同与实现边界

HG-1A 当前数据结构在 `rasterfall_draw.h`，实施与验证见 [Draw/reference](hardware-graphics-hg1a.md)。
本阶段固定 WORLD/opaque/nearest-repeat/bottom-pivot/no-primitive-fog，以调用顺序和 primitive 顺序
保持稳定提交；HG-1B 为正常 static prop 提供 bundle handle/generation 与帧 pin，材质/纹理以 bundle 加表索引标识。
同步 fixture 仍可借用 backing；retained Draw 和可扩展 domain 仍由后续 checkpoint 落实。

| 数据 | 必需字段与边界 |
| --- | --- |
| View | camera body/view 快照、direction/pitch、viewport、focal、near、WORLD/VIEWMODEL depth domain |
| Mesh | stable slot + generation；immutable vertex/index backing、triangle-list submesh ranges、bounds、position_scale 和格式 |
| Material | base color、texture handle/generation、nearest/address、alpha、sidedness、form-light policy；不存在的纹理也须显式解析 |
| Draw | mesh handle、submesh/range、整数实例变换/底部 pivot、material override、scene-light Q8、fog policy、layer/domain、stable sequence |
| 诊断 | producer、asset/instance ID、eligible/migrated/legacy 数量与拒绝原因；不进入 gameplay |

Draw 不包含投影后的三顶点、area、bbox、裁剪扇形或 `u_over_z`。V0 每实例、每 submesh 一次提交；
不要求 instancing/indirect/bindless。HG-1A 可以暂借原 CPU backing，同步 lowering；引入延迟消费前必须冻结全部引用和状态。

HG-1B registry 拥有 CPU backing 与 generation；后续 backend cache 只拥有对应 GPU 资源。
resource reload/world unload 使新引用采用新 generation；旧 slot 不得在在途帧仍引用时重用。
冻结帧 pin 住 mesh、material、texture，直到 GPU 完成且 CPU replay 不再需要它们才释放。
resize 只重建尺寸相关 target；device 重建使 backend cache 失效，不改变 CPU 定义。
后续 GPU cache 须满足静态资源首次上传后稳态上传量为零；实例/light overrides 写帧数据。
正常 static prop 两个实例不共享可变状态，尚无 normal GPU mesh 上传。HG-2A 的独立 proof 持有单 mesh/texture，逐 draw 仅写 push constants；尚未适配 registry generation 或接入 Core 帧 pin。

## 数值与顺序冻结

- 模型位置为整数、normal 为 Q15、UV 为 unsigned Q16。普通 prop 当前有效 scale 为
  `profile.render_scale_milli * instance.scale_milli / 1000`，再将位置乘 scale 除 1000。
  `position_scale` 是资源单位元数据，不能凭它额外重复缩放已解析的 profile。
- yaw 先归一到 `[0,360)`，sin/cos 乘 1024 后截断；先对模型 X/Z 做 Q10 整数旋转，再缩放/平移。
  Y 使用 `(position_y - model.min_y) * scale / 1000 + base_y`；普通 prop 不套用 rigid attachment 的 pivot。
  有符号整数除法保持 C 向零截断，不合并运算。每个源 normal 分别旋转并转 short 后，再三法线求和除 3。
- World Lighting V2 在实例原点采样一次；form light 是逐三角形常量。flat/textured 颜色调制、Q8 除法、
  clamp 和舍入顺序由现有 triangle helpers 保持，不能换为 Gouraud 或逐像素 Lambert。
- 必须冻结 gallery facing/sy/cy、gallery lighting、scene override、强制 culling、解析后的 texture/material
  alpha/sidedness、material light/tint/ambient/specular 和 bilinear policy。其他模型的 sphere/toon/skin/rigid scope
  不得泄漏进普通 prop；未支持的 policy 显式拒绝。
- WORLD near=64，focal=`width*3/4`；先 near clipping 后整数投影。顶点 inverse-Z=`1048576/z`，
  屏幕空间插值后转整数；clear=0，compare=`>=`，相等深度后提交者覆盖。
- WORLD 完整收集后 opaque/transparent stable partition；保留各类内部原顺序。
  透明 texel alpha × material alpha / 255，source-over，depth test 但不写 depth。
  Draw/Raster 混合不能越过原序列边界重排，不允许重复绘制。
- 普通 static prop gallery primitive fog 为零；Post Fog 独立执行一次。
  VIEWMODEL near=192、有独立 inverse-depth 与 coverage；Post 根据 coverage 跳过，不借用 WORLD depth。

HG-1A 对旧 producer 与新 lowering 的规范化命令、顺序、完整 color/depth 要求完全一致。
比较指针字段时比较资源内容/身份，不比较进程地址或结构 padding。
fixture 必须覆盖 yaw/scale、缺失纹理、单/双面、近面交叉、scene light 与两个实例。

## HG-2 混合 target 强制门禁

同一 Vulkan device，首版单 graphics+compute+present queue、单帧在途。HG-2A 独立离屏 indexed draw，
HG-2B 验证 compute 前段 → GPU export → graphics → GPU import → compute 后段 → Post/overlay/present。
compute shader 已支持独立 CLEAR/LOAD_EXISTING 与执行范围；clear/sky 仅由起始段消费。
现阶段为 hosted 交错诊断，已绑定 graphics bridge，尚未接入 Core 有序混合帧。

Target 合同必须描述 extent、format、color encoding、depth encoding、内容有效性、owner 与访问转换。
现有 color/depth/post_color 是 storage buffer，不能直接充当 attachment。
graphics color/depth image 需要查询格式能力，明确 load/store 和 compute/transfer/attachment/early-late-depth
barrier、layout transition。采用 GPU 内量化 inverse-depth ↔ D32 数值转换候选方案；不能 bit-copy 或默认认为浮点 reversed-Z 等价。

硬件覆盖误差只允许事先定义的边缘范围；遮挡错误独立判失败。HG-2B 必测同深度后画覆盖、远处薄墙、
近面交叉、compute/graphics 交错遮挡、透明后段、VIEWMODEL、Fog/Post、resize/swapchain 重建。
unsupported preflight 不能留下半帧；strict 禁止 readback/CPU copy，并单独识别“要求 hardware 却意外 lowering”。
此门禁通过前不得接入 normal-frame hardware props。

## 基线复现与口径

先 `windows/NativeCodex.ps1 package`，构建根 Makefile 的 `win-gpu-raster-diff-test` 到 `build-windows/`，
再运行 `powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_baseline.ps1`。
使用同一 MSYS2 MinGW lane；独立 differential 是 hosted 工具，不加入 normal package 或 freestanding libc。
脚本现在先执行完整 differential suite，失败即停止，不再只以 selected world replay 判定基线通过。
`-Checkpoint HG-1A-preflight` 可标注本次 manifest；省略仍标注 HG-0。独立 suite 从仓库根运行，
以便保留 `build/` 下的 replay self-check；游戏仍从 package 目录加载资产。
在 Windows PowerShell 中，也可直接执行与该 Makefile 目标相同的编译命令（从仓库根运行；MSYS2 默认安装路径）：

```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& C:\msys64\mingw64\bin\gcc.exe -std=c11 -O2 -Wall -Wextra -Werror `
  -I gpu/include -I windows/include -I include -I include/tlibc -I rasterfall/include `
  gpu/src/rf_gpu_raster_diff_test.c gpu/src/rf_gpu_raster_cpu_ref.c `
  gpu/src/rf_gpu_renderer_hosted_shim.c lib/graphics/renderer.c `
  gpu/src/rf_gpu_vulkan_backend.c gpu/src/rf_gpu_raster_pack.c `
  gpu/src/rf_gpu_raster_bin.c rasterfall/src/rf_gpu.c `
  -o build-windows/rf-gpu-raster-diff-test.exe
if ($LASTEXITCODE -ne 0) { throw 'differential build failed' }
```

脚本要求新输出目录，默认 `tmp/hg0-时间戳`，不提交生成物。避免同时运行另一份 package 程序写入同一日志。

| 场景 | 口径/生成物 |
| --- | --- |
| `--help`、`--logic-test` | 当前 CLI 与逻辑门禁、退出码 |
| normal-frame audit | camera=(-13000,-12000)、direction=(0,1024)、1280×720；CPU BMP、scene 分项和总命令 |
| world stream near/mid × 0/30 | 离屏 camera=(0,-3400)/(0,-8400)、y=-350；selected Raster ABI + texture sidecar；CPU/compute BMP、完整 signed depth、hash、mismatch 报告 |
| normal CPU、strict native、strict Fog，near/mid | 每轮 46 帧，丢弃前 16 帧；保存实际 camera/extent 与 frontend、fence、present wait 中位数/P95 |
| wave | 显式 `--map rasterfall/assets/maps/rasterfall.map`，320 帧实际波次；校验 world=1 且出现活敌，逐帧 strict path/order/readback/copy 门禁；波次性能不与固定场景合并 |

注意：当前 `--gpu-normal-scene near/mid` 只设置初始 camera；session 随后从本地 actor 恢复位置，
稳定帧实际是 Campaign spawn `(-13000,-12000)`。不能把这两轮命名当作两个稳定距离；真正 near/mid 对照来自离屏 world stream。
normal-frame audit 的 later 包含 flags/enemies/AI/text 等，不能等同正常帧 world-submission。
stream 是 selected supported commands，不能当作完整 normal frame。WORLD、retained 和 scene 命令也分别记录。

HG-0 修复显式 `--frame-audit` 的慢帧抽样：现在逐帧输出。此前仅在间隔≥50ms 或每 60 帧首帧记录，
会偏置快帧统计。新基线包含审计开销，不能直接与旧抽样日志计时比较。
Windows runtime log 为追加写入，脚本只提取本次新增部分，不删除既有日志。
manifest 保存 commit、dirty 状态、exe/differential SHA256、package 资产逐文件 SHA256、GPU/driver、命令及退出码。
native adapter 还应核对原始运行日志；设备清单不等同于实际选中的 adapter。
WMI/CIM 不可读时从 Windows display-class registry 读取 driver；保留查询失败原因。
`--gpu-wave-repro` 不会自动选择 Campaign，省略显式地图会在默认 Outpost 运行，不构成正式地图波次基线。

## 进度与后续验收

HG-0 本次证据见 [checkpoint 记录](hardware-graphics-hg0.md)。根计划同步记录各 checkpoint 状态。
HG-1A 已先修复 HG-0 遗留的 CPU planar vertex-lit 透明度与深度差异，见 [前置修复记录](hardware-graphics-hg1-preflight.md)。
HG-1A 的普通 opaque static RMESH Draw/reference 与 Windows Intel 精确回归已完成，见
[验收记录](hardware-graphics-hg1a.md)。HG-1B registry、generation、帧 pinning 与释放见 [资源生命周期](hardware-graphics-hg1b.md)。
HG-2A 已完成独立 graphics proof，数值误差、近面深度待验证边界与复现见 [HG-2A](hardware-graphics-hg2a.md)；下一步是 HG-2B。
HG-2B 的[整数深度与 target bridge](hardware-graphics-hg2b.md)及真实 compute/graphics 交错验证通过，Core 顺序/native 门禁仍待实现；HG-3A/3B、HG-4A/4B、HG-5A/5B 尚未开始。
`--frame-audit` 的 `draw-reference` 已统计实例、submesh Draw、`cpu_lowered_triangles` 和 legacy 拒绝原因。
资源上传、instance upload、bridge bytes/time、unexpected_lowering 仍在对应 owner 实现时加入，
不以占位零值伪装已实现 hardware 数据。
