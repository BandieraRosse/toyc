# GPU Scene 阶段 0：现行画面覆盖矩阵

> 状态：阶段 0 工作记录；目标接口尚未实现
>
> 核对日期：2026-09-23
>
> 范围：正常运行帧；Campaign 和诊断内容单独标明

本表是[活动计划](gpu-scene-renderer.md)第一步的源码盘点。`producer` 是 mixed frame 的诊断标签，
不是 pass 或材质分类。当前执行链仍为 Draw/RasterCmd mixed frame；下表的目标归属不表示 GPU Scene
已经实现。画面层序和资源所有权以[渲染架构](../architecture/rendering-architecture.md)及
[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)为准。

| 可见内容 / 入口 | 现行表示与材质、深度语义 | 新路径归属 / 阻断项 |
| --- | --- | --- |
| SKY：`rasterfall_sky_draw`，`render_scene` | 直接绘制背景；Core 记录 SKY，mixed executor 根据 sky 标志清屏；无世界深度写入 | SKY pass；冻结颜色/相机规则，不能把背景当作 WORLD 网格 |
| WORLD 地面、边界、墙、box、ramp、platform、static RMESH：`render_scene`、`render_static_props` | 持久 ground/map、普通不透明 static RMESH 可为 Draw；其余几何仍可为 RasterCmd。`WORLD_MAP` 是默认标签。静态世界光照 Q8、模型 form/material policy 参与最终颜色；opaque 深度测试并写入 | WORLD opaque 实例和材质；覆盖非持久地图类型、条件性 air gate、procedural 几何与静态模型 fallback。不能只迁移已持久化的 mesh |
| WORLD 地图透明：`render_platform`、`draw_box_alpha` | platform 与 air gate box 有 authored alpha；Core 从 WORLD 稳定分到 TRANSPARENT。source-over、深度测试、不写深度 | WORLD transparent 有序项；保留原提交序号和条件开关。覆盖平台/air gate 的固定 fixture |
| WORLD 角色 body：`render_enemies`、队友、managed/network actor | 普通模块化 body 可为动态 Draw，finalized pose 后 GPU skinning；其他 body/特殊敌人和材质路径可能仍降为 RasterCmd。标签主要为 `ENEMY_BODY`，特殊 rigid 有独立枚举。材质含 base texture、sphere、toon、edge、alpha、双面、角色 visibility floor、实例衣裤色 | WORLD opaque/transparent 角色实例；GPU Scene 要承接 finalized palette 和材质覆盖。逐项查清特殊敌人、edge、透明材质的 GPU 表示，不能按 `ENEMY_BODY` 标签认定全部 opaque |
| WORLD gear/weapon：`render_modular_ai_equipment` 等 | body 后按 actor 顺序提交；`GEAR`、`WEAPON` 标签。rigid 附件消费 pose cache/socket/placement、scene-light override；当前多为 RasterCmd | WORLD opaque/transparent 附件实例；保持同一 actor 的 finalized placement 和可见顺序，避免 body Draw 与附件 Raster 的 bridge |
| WORLD 投射物、交互物、旗帜、sign、world label/name/status | 投射物、交互物及 sign board 为世界几何；sign/flag 字面经 `render_world_text_plane` 生成 depth-tested 三角形。地图 `TOY_MAP_DRAW_LABEL` 在 `rasterfall_render_map_labels` 中投影为无深度屏幕文字，进入正式 OVERLAY color/coverage。actor name/status 在 `rf_core_begin_screen_overlay` 后绘制，属 OVERLAY | 几何和世界文字进入 WORLD；地图 LABEL 属 OVERLAY，保持无世界深度语义。actor name/status 属屏幕 OVERLAY，保持 near/distance 投影门槛及血条顺序。地图 LABEL 未转为有序 GPU payload 前阻断统一正常帧，不能当作已有 WORLD 命令 |
| EFFECTS：`rasterfall_render_effects` | 射线、billboard、死亡碎片/尘埃等 RasterCmd；碎片和尘埃强制有序透明、不写深度。粒子入口仅在返回正像素且无新增命令时累计 EFFECTS direct pixels。`render_effect_overlay` 实际由帧尾 `rasterfall_render_overlays` 调用，在 OVERLAY 中直接改像素 | WORLD effects 进入 EFFECTS pass；damage flash 等屏幕覆盖进入 OVERLAY。两类分别保持提交与 source-over 顺序，不能因 effect 数据来源相同而合并 pass |
| VIEWMODEL：`rasterfall_viewmodel_render` | 手、武器、药品、枪口效果为三角命令；Core 使用独立 near/depth/coverage，透明子项有序混合 | 独立 VIEWMODEL depth/coverage pass；不得共用 WORLD depth，保持手/武器/枪口层序 |
| POST、OVERLAY：`rf_core_begin_screen_overlay` 后的 HUD/UI | normal Post disabled；HUD、菜单、准星等绘入 CPU overlay color + 8-bit coverage，native presenter 上传并合成；不写 WORLD depth | 保留 POST 语义边界和 OVERLAY pass；阶段 3 需明确定义 GPU 表示及覆盖，不能把现行 CPU overlay 上传误报为统一 GPU Scene 已完成 |

## 横向合同与现行拒绝条件

### 地图表示与条件内容核对

| 来源条件 | 当前执行 | 新 Scene 必须保留 |
| --- | --- | --- |
| wall、普通 box、ramp；`style == 2` 且非 air gate 的 platform | 只有 mixed frame、runtime map 已加载且诊断平面开关关闭时，才由 `persistent_map_map_class_for_draw` 进入持久 Draw mesh；否则走普通地图绘制 | 同一内容在新路径中有 mesh/instance 表示；不能把旧 Draw 命中率当成完整地图覆盖 |
| `air_gate_` box/platform | 不进入上述持久 mesh；`active_session->air_walls_enabled` 决定是否提交，box 以 authored alpha 48 绘制 | 冻结条件开关、透明序号和无深度写入语义 |
| 地图 LABEL、SIGN | LABEL 在世界/viewmodel flush 后进入 OVERLAY；SIGN board 是几何，文字随后作为世界三角形提交 | 两者使用不同表示与遮挡规则，不能合并为一种世界文字实例 |

`rasterfall.map` 的 runtime render 记录已覆盖这些类型；地图 parser、Runtime Map、兼容
`toy_map_draw` 和 GPU Scene 的身份/顺序仍须各守其边界。

- **纹理/材质：** 模型可用 base texture、sphere mode 1/2/3、toon texture/shared toon、edge、
  ambient/specular、tint、alpha 与双面标志；地图还可用贴图/顶点光照。当前 Raster pack 的 Core
  classifier 只接受有限纹理子集：有效 3/4 通道纹理、宽高至多 8192、尺寸匹配；bilinear、
  material features、toon、多纹理、add/tint 等会被判 unsupported。新路径必须按实际出现的材质
  明确支持或形成阻断决策，不能借旧 classifier 推断场景不存在这些材质。
- **裁剪/光照：** WORLD 使用 `NEAR_Z` 逐三角形近裁剪和保守可见性；VIEWMODEL 有独立 near。
  normal world light 来自 Static World Lighting V2 Q8 查询，normal fog 为中性值。新路径的
  深度约定、near 穿越、实例光照与角色材质 floor 都要进入画面合同。
- **整帧失败与 pin：** Core 在 target 写入前冻结、pin、preflight；Draw 的资源 handle/generation
  由 registry/cache 解析，mixed slot 在完成后释放 pin。Raster 纹理另由当前帧拥有，仍使用
  texture-view pointer identity 打包。新 Scene 不能持有裸指针，必须覆盖 Raster 资源与 slot
  退休。`edge`、`overlay`、无效纹理/命令、direct pixels、consumer 失败均可使 required 帧失败。
- **标签局限：** `RF_CORE_PRODUCER_*` 共九类，`WORLD_MAP` 还覆盖 scene、world text 和交互物；
  `ENEMY_BODY` 还覆盖多个 actor 来源。透明由命令属性和 Core 的稳定分段决定，不能由标签决定。
- **LABEL 覆盖：** 固定镜头暴露了旧直接像素被延迟 WORLD flush 覆盖的问题；现已移到正式
  overlay color/coverage。CPU/native 固定镜头必须看到 WORLD_LABEL；它不写 WORLD 深度。
  新 Scene 的有序 overlay payload 尚待接入，不能把当前 CPU overlay 上传当作完成迁移。
- **现有内容证据：** 正式 `rasterfall.map` 包含 `render ... kind=label`（例如
  `return_outpost_label`、`legacy_render_003`）和 `kind=sign`。`rasterfall_map.c` 把 runtime render
  按原顺序投影为 `level_map.draw`；因此 LABEL 并非仅存在于旧格式或开发测试场景。

## 阶段 0 待核对证据

定向输入已建立为[专用渲染地图](../guides/gpu-scene-fixture.md)，提供 opaque、透明 air gate、
LABEL/SIGN、近处 box 与远处薄墙。它通过现行 mixed 路径采集证据，不代表新 Scene 覆盖；
air gate 开/关已各有固定镜头；特殊材质、角色与附件、effects/UI 仍须各自冻结输入和验证。一个镜头不能签收全部地图条目。

1. 阶段 0 退出时用固定 near 0/30/60、Campaign、mid、thin-far 的 frame audit，记录各层
   Draw/Raster span、unsupported 与 direct-pixel 的**实际出现值**；地图 LABEL 须按固定 overlay 镜头
   检查，源码列举和审计零计数都不能证明其 GPU 覆盖。
2. 为世界文字/状态、条件性地图内容、特殊敌人与所有 EFFECTS 记录固定 capture，确认遮挡、
   alpha、材质及 near 边界。任何未列入本表的正常帧可见内容先补归属，再冻结阶段 0 合同。
3. 按[活动计划](gpu-scene-renderer.md)后续步骤冻结 GPU Scene 接口、画面差异政策和可复现基线。

## 源码核对入口

- `rasterfall/src/rf_game_runtime.c`：`rf_game_runtime` 的 WORLD → EFFECTS → VIEWMODEL 编排。
- `rasterfall/src/rasterfall_render.c`：`render_scene`、角色 body/gear/weapon、透明地图、effects 和近裁剪。
- `rasterfall/src/rf_core_host.c`：命令拒绝分类、分层、整帧 preflight、overlay/slot 编排。
- `rasterfall/include/rf_core_mixed_frame.h`、`rasterfall/src/rf_core_mixed_frame.inc`、
  `gpu/src/rf_gpu_mixed_executor.c`：producer 枚举、Draw/Raster span、资源解析与执行。
