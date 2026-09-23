# Rasterfall 渲染架构

> 状态：当前
> 所有者：Rasterfall renderer frontend、Core Host 分层编排
> 最近核对：2026-09-23

本文定义 CPU/GPU 共用的场景提交、帧分层和所有权。GPU executor、资源与 presenter 合同见
[GPU 渲染架构](gpu-rendering-architecture.md)；角色表现见 [角色表现](character-presentation.md)；HUD、
特效和 viewmodel 见 [HUD 与特效](hud-effects.md)。

## 所有权

| 职责 | 所有者 |
| --- | --- |
| world/角色/地图图元、投影、近裁剪与 draw command | `rasterfall/src/rasterfall_render.c` |
| frontend state、默认纹理与 worker binding | `rasterfall/src/render/rasterfall_render_frontend.c` |
| 底层 command、光栅化和 recording context | `lib/graphics/renderer.c`、`include/toy_renderer.h` |
| renderer/window/surface 生命周期、层 barrier、flush 与 present | `rasterfall/src/rf_core_host.c` |
| normal frame 的 camera 与 presentation state | `rasterfall/src/rf_game_runtime.c` |
| 天空、HUD、viewmodel、effects、性能统计 | 各自 `rasterfall_sky.c`、`rasterfall_hud.c`、`rasterfall_viewmodel.c`、`rasterfall_effects.c`、`rasterfall_perf.c` |

renderer 只读玩法或派生展示状态，不修改 `toy_game`。客户端位置与朝向可使用 presentation cache 插值，
但 HP、武器、downed、动画和统计仍从 actor 真值投影；HUD、第一人称武器和受击效果不得回读旧的顶层
玩家副本。

`rasterfall_render_bind()` 是既有串行 presentation context，只向旧 helper 提供 session/effects/net、
纹理和 world light；它不拥有 window、surface、present 或 Core 资源。并行模型录制优先使用
`toy_renderer.recording_context` 隔离 frontend state。

## 一帧的数据流

```text
Core begin / clear
  -> sky
  -> WORLD: scene, map, actors, world labels
  -> world/transparent ordering barrier
  -> EFFECTS
  -> VIEWMODEL (独立 depth/coverage domain)
  -> optional Post semantic boundary
  -> Core begin screen overlay
  -> OVERLAY: prompt, names, crosshair, HUD, menus, console, desktop
  -> Core final flush / present
```

层序固定为 SKY、WORLD、TRANSPARENT、EFFECTS、VIEWMODEL、POST、OVERLAY。调用位置不能隐式改变层；
`rf_core_render_frame_enter_layer_v1()` 的 cursor 拒绝跳层和回退，审计必须保持
`invalid_layer_transitions=0`。

Core 在 pre-post 各层收集原始 command，完成整帧可表达性判断后才允许 GPU native 路径。任何 unsupported
material/texture/edge/overlay、direct-pixel debt 或 consumer failure 都触发完整 CPU replay；禁止 world
已写 GPU、后续层却只写不会呈现的 CPU surface。screen overlay 开始后，Core 同时切换返回 surface 与
`renderer->surface`；GPU native 使用独立 XRGB8888 color + 8-bit coverage，再 source-over composite。

## Command 与透明语义

WORLD 可跨多次 flush 收集，再稳定分为 opaque、transparent 两段。透明保持原提交顺序，使用 source-over、
depth-test/no-depth-write；有效 alpha 为 `texel_alpha * material_alpha / 255` 向下取整。当前不做 OIT 或
自动深度排序。VIEWMODEL 使用相同透明像素合同，但拥有独立逆深度域和 coverage。

actor 屏幕裁剪由 `ai_actor_command_scope_*()` 拥有。command filter 必须在每次 flush 消费前处理当前段，
随后从新缓冲零位置继续；actor 结束处理尾段并解除 callback。producer 身份、层和透明 state 是不同概念，
不能互相替代。

Texture V1 每帧维护唯一 texture-view/descriptor/texel 表，以 pointer identity 去重并使用 1-based handle；
CPU pointer 不进入 ABI。禁止为每个 textured command 回扫全部历史 command。surface stride 在 Core 边界
从字节显式换算为 GPU API 使用的 32-bit 元素数。

## 世界几何与光照

地图文本、Runtime Map、碰撞体和可见几何彼此独立。`boundary_wall`、partitioned floor、地图 wall/box/
ramp/platform 和 static RMESH 在各自 record 入口做保守 frustum/AABB 剔除；穿越 near plane 的几何仍交给
逐三角形裁剪。地图 visual mesh 不替代 gameplay collision。

Static World Lighting V2 是 normal runtime 唯一 world-light 来源。renderer 只消费其 Q8 查询结果，按
`world light × form lighting × material policy` 形成最终提交颜色；normal runtime 的 fog 输入固定为 0。
field bake、固定参数和诊断例外由 [Static World Lighting V2](static-world-lighting.md) 拥有。

static RMESH 使用实例 world origin 查询一次 scene light，再通过既有 override 传给模型提交；不在逐顶点
热循环重复查询。角色材质的 FACE/SKIN/EYES/HAIR visibility floor 属于 character render policy，非角色
RMESH 保持通用 form-lighting。资产颜色、材质 role 与导入合同不由 renderer 重新定义。

## Post 与 CPU reference

normal runtime 保持 Post disabled，presentation color 直接选择 raster color。底层 identity/fog fixture
可使用独立 `post_color` 验证 ABI；它随 extent 重建，不能与 Raster target 原位读写。保留的 fog command
和 Post Fog 只用于兼容与 differential，不是正常画面策略。

CPU renderer 是独立完整 reference。GPU partial replay、world stream capture 或 readback 只用于诊断，
不能宣称为完整 normal frame 或性能结果。可执行验证入口见
[视觉验收](guides/visual-validation.md)和[渲染性能诊断](guides/rendering-performance.md)。

## 修改落点

- 世界物体缺失或遮挡：场景对应 `render_*`、近裁剪、depth 与层 barrier。
- 地图几何或剔除：map projection/record 入口；不要修改碰撞真值。
- 光照、材质或纹理：world light consumer、model submission、Texture V1；资产解码在 `lib/assets.c`。
- GPU 执行、bridge、resource 或 present：转到 GPU 架构与 GPU 验收指南。
- 可见结果改变：补固定 capture 或像素 differential；并行路径同时比较单 worker 与多 worker。
