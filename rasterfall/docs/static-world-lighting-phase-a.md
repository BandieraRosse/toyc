# Static World Lighting V2 — Phase A 开发记录

> 文档更新：2026-09-14
> 源码核对基线：Phase A 前工作区 `5ee2811`；直接核对 renderer submission、floor、static prop 与 rasterizer。

## 编码前审计：实际 Lighting V1

1. `rasterfall_render.c::bake_static_lightmap()` 在 renderer bake 入口生成缓存，生命周期由 Game 的 render context 持有，world load/switch 后重烘焙。
2. 网格为 **32×24 XZ**，覆盖 `session.level` bounds；单元中心计算，完全不使用 Y。
3. 查询是整数坐标定位单格并 clamp 边界，无插值（nearest-cell）。
4. `draw_partitioned_floor()` 的正常地面与 floor paint 在 `fixed_floor_lighting=1` 下使用 256，且关闭雾；ramp/platform 等 planar surface 走普通三角形中心采样。
5. 地图 primitive/quad/boundary wall 经 flat/textured/alpha triangle helper 采样网格；既有 box 面颜色混合仍在几何提交层。建筑 RMESH 走 static prop 路径，行为不同。
6. static RMESH prop 在 `rasterfall_render_static_prop()` 中设 `active_gallery_lighting=1`，环境因子为 256、雾为 0；目前不消费 baked world brightness，但消费 form lighting。
7. procedural actor/enemy 的普通 primitive triangle 消费网格；感染 RMESH 沿 enemy triangle helper 消费网格。RFCHAR/skeletal 模型由实际 submission 的 gallery/fixed/scene override 决定，非 bypass 时同样采样网格。没有独立 actor lighting state。
8. RMESH/RFCHAR/weapon 的 form lighting 位于 `render_gallery_model_range()` 的 triangle submission；法线变换后计算 `model_form_light_q8()`，特感 rig 也复用该函数。
9. `character_render_policy()` 解析 material role，flat/textured helper 在 scene × form 后执行最低/最高亮度保护。无 role flat 模型先量化 form 调色，再由 rasterizer 调制 scene；本轮不能重排这两个整数阶段。
10. bake 仍使用基础亮度 `270 + east gradient(0..4)`，BOX（排除 air_gate）900 RFU 内减亮；固定 `(4000,-160)` 的 2600 RFU 半径增亮仍存在，注释称 east warm point light，但实际只有标量增亮，没有暖色色相。最终 clamp 为 150..286。
11. rasterizer `shade_color()` 先 light 调制/通道 clamp，再 fog 混色；textured face 保留既有 fog/2。fog 不是 world light sample 的一部分。

视觉基线为根目录 `campaign-environment-review/README.md`、sheet 与十二张 PNG；仅只读使用。地图、碰撞、资产与材质策略不在 Phase A 修改范围。

## Phase A 边界与后续接点

`rasterfall_world_light.h/.c` 拥有 bake、缓存定义、position sample 和 Q8 组合；render context
仅提供缓存存储，旧 bake facade 保留生命周期入口。原 flat/textured/alpha helper 的 world
查询迁移到新 API，三角形仍取未裁剪世界坐标中心（现在同时传 Y），无新增 actor 采样。
主地面、static RMESH、gallery 和诊断 override 保留 renderer 原策略。

下一步没有必须回退本接口的结构阻碍：position 已包含 Y，sample 独立于对象和材质。
但 Phase B 需给 bake 提供 Runtime Map 的明确三维遮挡数据；当前输入仍是 V1 的
`toy_map` projection，BOX/XZ proximity 与 role 排除规则不能直接充当 3D sun visibility。
同样，旧 proximity 与固定东侧增亮目前仍混入 environment；拆分时应明确新因子语义并避免重复减亮。
static RMESH 和主地面的固定亮度策略也需后续单独决定接入，不能仅升级 field 后假设所有对象已消费。
无 role flat 材质的两次整数调色仍保留，统一数学阶段若要改变舍入应另做视觉验收。
