# Static World Lighting V2 — Phase C1

> 文档更新：2026-09-14
> 源码核对基线补充：Phase C3 将共用 scene override 更名为 `active_scene_light_override_q8`，V1 转为独立诊断 owner；本页其余内容记录该阶段的接入边界。
> 源码核对基线：Phase B checkpoint `629c318` 后的 Phase C1；核对正常 map static prop submission、模型 form/material helper 与 environment capture。

## 路径与所有权

正常地图的 industrial props、facility/power-yard equipment 和 `env_arch_*` architecture RMESH
统一经 `render_static_props()` → `rasterfall_render_static_prop()` → `render_gallery_model_range()`。
没有独立的正常 facility RMESH 提交入口。设施由地图中的多个独立构件实例组成。
`rasterfall_render_rigid_resource()` 是 rigid attachment/model transform 入口，当前消费者属于
角色装备和附件，不属于 map static prop；不在此扩大消费范围。

模型顶点准备阶段已有 yaw/scale 后的 world position，triangle submission 拥有 transformed normal，
`model_form_light_q8()` 仍负责表面立体感。gallery/model preview、arch/campus fixture、lighting-props
等独立诊断直接调用各自的模型/prop 提交入口，不经过正常 map prop 遍历。

## 接入

`render_static_props()` 在 Runtime Map 已加载时，针对普通 RMESH 每实例按
`(instance.x, instance.y, instance.z)` 查询一次 `rasterfall_world_light_at()`，以
`rasterfall_world_light_v2_q8()` 合成为 scene 因子；Y 是既有 `-900 + map_prop.y`。
field 仍为 Phase B 的 ground-following XZ field，传 Y 不增加采样层。

scene 因子临时通过已有 `active_scene_light_override_q8` 进入模型 helper，提交后恢复。
它优先于 gallery 的固定 256。纹理 RMESH 使用 scene × form；无 role flat 材质保留原
form 调色再乘 scene 的两阶段整数舍入。form、material policy、fog 的职责与原实现不变；
static prop 原 gallery 无雾策略保留。

RMESH 不开启 `active_world_light_v2` planar scope，避免 Phase B flat vertex queries 进入
普通模型热循环。boundary wall 仍只开启原 procedural V2 scope，不使用 RMESH scene override。
不改 field/bake/参数、RMESH 格式、模型网格、角色、viewmodel 或 lighting assets。

## 细采样边界与验证

普通小型 prop 与建筑构件均使用实例单点；west-facility、east-facility、power-yard、
north-facility 的实际 before/after capture 未显示必须升级细采样的明显整件跨边界错误。
因此本阶段没有 triangle/vertex world-light 系统。后续只有实际 capture 证明问题才考虑升级。

复用 `--logic-test`：static prop lighting 回归检查实际 textured RMESH command 的 scene × form，
覆盖开放与背阴环境、ambient 与亮面 form。复用 `--environment-capture ... --textures`、
`tools/environment_sheet.py`、`lighting-props` 和重复 BMP 字节比较。
world capture 输出已有 commands/triangles，并补充 frame/raster 微秒计时；frame 包含 scene
submission 与 flush，不含 begin、BMP IO、present 或 pacing，首视角还包括懒加载，不能当作
游戏 FPS。性能比较需相同二进制构建参数与固定视角，分离首视角加载成本。

本轮可复核生成物、日志、性能与视觉审查放在 `tmp/static-world-lighting-v2-phase-c1/`。
Phase C1 到此停止，未进入 C2 或最终参数调节；checkpoint 由用户审查后单独提交。
