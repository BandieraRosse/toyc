# Hardware Graphics HG-4：Ground / Map geometry

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)

> 文档更新：2026-09-21
> 源码核对基线：2026-09-21 当前工作区；HG-4A Ground 与 HG-4B map/boundary 均已按 Windows Intel 签收。五类几何已接入持久 Draw/GPU geometry；专用 runtime fixture、五类 CPU/native 对照、Fog/四 extent resize、Outpost/Campaign/WHU/Campaign world-cycle、Windows package 与完整逻辑回归通过。HG-4 完成。

## HG-4A 当前实现

正常 strict mixed world 在 world generation 首帧把 partitioned floor、Campaign/WHU paint precedence、
spawn priority 与 1024 RFU V2 lighting subdivision 固化为 renderer-owned immutable mesh。资源通过
`rasterfall_resource_registry` 持有并随既有 world invalidation 退休；后续帧不再按相机重建地面几何。
CPU、非 mixed 与显式 flat-planar diagnostic 保留原 RasterCmd producer。

地面 mesh 按颜色与 16000 RFU 空间块合并 submesh，顶点使用局部坐标，material 中的 renderer-only
平移只由 HG-4A consumer 解释。U 分量携带 Q8 world-light，graphics shader 使用 `noperspective`
插值，保持 planar vertex-light 的屏幕空间语义；该模式无纹理、无 primitive fog，scene light 固定为 256。
它不改变 Runtime Map、collision、floor paint 顺序或 gameplay truth。

`--frame-audit` 新增 `ground-draw`：`items`、`triangles`、`legacy_commands` 与 `mesh_builds`。
Intel 1280×720 near/0 的 120 帧 strict native 结果为：首帧 `mesh_builds=1`，之后为零；每帧
162 Draw / 21366 source triangles，`floor_cmd=0`、`legacy_commands=0`；稳态 `gpu_upload_bytes=0`，
120/120 `gpu-native`，零 fallback/readback/CPU framebuffer copy/hot queue-idle。

Fog strict native 同样完成 120/120 帧。四 extent 的 140 帧 resize gate 已扩展为同时检查
`ground-draw`：全程保持 162 Draw / 21366 triangles、总 `mesh_builds=1`；两个 frame slot 首次绑定后
`gpu_upload_bytes` 持续为零，swapchain recreate 不重建或重传 ground mesh。`--logic-test` 现覆盖
生成资源 adopt、重复 identity 拒绝、在途 pin、world invalidate 后同名新 generation 与旧 generation
延迟退休释放。

真实 runtime world-cycle gate 现通过 `--world-cycle-gate` 在第 30/60/90 帧依次执行
Outpost → Campaign → WHU → Campaign。`tools/hardware_graphics_world_cycle.ps1` 强制检查 120/120
strict native、四个 ground mesh generation、各 world 两个 frame-slot 预热后零上传、零 fallback/readback/
CPU framebuffer copy，以及每次切换时旧 generation 同时处于 retired + pinned、随后被延迟释放。
首轮门禁实际发现并修复了两个边界：producer 不能把“仅供旧在途帧解析”的 retired handle 当作当前
active handle 复用；WHU 的 -160000 RFU 南缘要求 graphics Draw 坐标合同覆盖 Runtime Map 的
240000 RFU room_limit。当前门禁最终 releases 单调增长且 retired 回到零。

## HG-4A 视觉签收

`tools/hardware_graphics_ground_capture.ps1` 串行生成 Campaign base/spawn/west-facility 与 WHU
A18/B 广场/分馆前场/D→E/F 的 CPU PPM、便于审阅的 CPU BMP、strict native GPU BMP、日志、哈希与
全帧像素差 JSON。2026-09-21 Intel 实机七组均完成；原尺寸与组图审阅确认相机、paint precedence、
spawn priority、地面接缝和掠射轮廓一致。全帧差异为 16.16%--59.72%，主要来自既有 CPU 蓝色天空与
strict native 黑色天空，不能解释为 ground 误差。

同组最后一帧审计中，Campaign CPU floor producer 为 2624--3090 RasterCmd，strict native 固定为
162 Draw / 21366 triangles / 0 legacy ground command；WHU CPU 为 4988--6946 RasterCmd，strict native
固定为 212 Draw / 73390 triangles / 0 legacy ground command。七组 native capture 均为零普通 readback、
零 CPU framebuffer copy、零 hot queue-idle；单次显式 capture readback 只用于保存验收 BMP。

## HG-4B 实现

正常 strict mixed world 将 wall、非 air-gate box、ramp、opaque 非 air-gate platform 与 procedural
boundary wall 分成五个 renderer-owned immutable mesh。mesh 沿用 HG-4A 的 1024 RFU V2 lighting
细分、16000 RFU 局部坐标块、U 分量 Q8 vertex-light、resource generation/pin 与延迟退休合同；每类
首次实际进入相机可见遍历时构建一次，后续帧只提交持久 Draw。颜色与局部坐标相同的 patch 合并为
submesh，避免逐 patch GPU resource/cache entry。

动态 air-gate box、透明 platform、texture wall、CPU/非 mixed 和显式 flat/no-planar diagnostic 继续走
原 RasterCmd producer。这是有意的回滚边界：当前 Draw 合同只接收不透明无纹理几何，不改变
`air_walls_enabled` 或透明无深度写语义。

`--frame-audit` 新增 `map-draw`，按 wall/box/ramp/platform/boundary 输出 `items/triangles/mesh_builds`。
Windows 实测：Campaign boundary 为 469 Draw / 22876 triangles；WHU box 为 34 Draw / 7952 triangles。
`rasterfall/assets/maps/hg4_map_geometry_fixture.map` 通过正常 V1 Runtime Map/projection 链路提供 wall、ramp
与 opaque platform，三个 `--gpu-normal-scene hg4-*` 固定相机分别得到 wall 1 Draw / 24 triangles、
ramp 4 Draw / 46 triangles、platform 1 Draw / 12 triangles；各进程仅首帧 build 一次，之后为零。

`tools/hardware_graphics_map_capture.ps1` 串行生成五类 CPU PPM/审阅 BMP、strict native GPU BMP、日志与
哈希。2026-09-21 Intel 实机五组均为 30/30 strict native，零 fallback；原尺寸审阅确认 wall、box、ramp、
platform、boundary 的轮廓、遮挡、坡面与平台高度一致。CPU 蓝色天空与 strict native 黑色天空仍是既有
全帧差异来源，不属于 map geometry 偏差。

## HG-4B 生命周期签收

Fog + 四 extent resize 的 140/140 strict native 门禁通过：ground 只 build 一次，当前 near 视角的
boundary 也只 build 一次；extent 变化不重建 world mesh，双 frame-slot 预热后 `gpu_upload_bytes=0`，
零 fallback/readback/CPU framebuffer copy。world-cycle 的 120/120 strict native 门禁通过：四个 ground
generation 与当前各 world 实际存在/进入视野的 map class 均只在 phase 首帧构建；统计为 wall 1、
ramp 2、boundary 2，旧 generation 在切换时可见 retired + pinned，最终 `retired=0`、loads=67、releases=41。

Windows package build、完整 `--logic-test`、PowerShell 语法检查与 `git diff --check` 通过。Linux/其他 GPU
没有在本 checkpoint 重复实机验收。

HG-4A 与 HG-4B 已完成，HG-4 签收。
