# Hardware Graphics HG-4：Ground / Map geometry

> 文档更新：2026-09-21
> 源码核对基线：当前工作区；HG-4A Ground 已按 Windows Intel 签收。strict native、Fog、resize、world-cycle、逻辑回归及 Campaign/WHU 七组 CPU/native 同姿态 capture 均通过；HG-4B map/boundary geometry 尚未开始，HG-4 整体未签收。

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

## 剩余验收

- HG-4B：wall/box/ramp/platform 与 boundary geometry，按 producer 独立迁移和回滚。

HG-4A 已完成；HG-4 整体在 HG-4B 前不标记完成。
