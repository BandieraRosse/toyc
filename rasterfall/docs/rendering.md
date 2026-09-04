# 渲染、HUD、特效与性能

> 文档更新：2026-09-04
> 源码核对基线：工作区（effect runtime 第一阶段基础抽象）

## 渲染边界

`src/rasterfall_render.c` 是世界渲染和角色渲染主体：投影/近裁剪、三角形提交、地面与地图图元、
拾取物、敌人、玩家/队友、骨骼角色、弹道粒子及模型诊断。公开入口在
`include/rasterfall_render.h`，共享状态由 `rasterfall_render_context` 绑定。

`src/render/rasterfall_render_frontend.c` 是渲染器前端适配，管理默认纹理、覆盖配置和 worker
绑定；底层光栅器在仓库公共的 `lib/graphics/renderer.c` / `include/toy_renderer.h`。

其他视觉模块：

- `rasterfall_hud.c`：玩家、网络、波次、商店 HUD，交互提示和 BMP/帧导出。
- `rasterfall_viewmodel.c`：第一人称手臂、武器模型、后坐/摆动和枪口位置。
- `rasterfall_effects.c`：消费 `rasterfall_effect_event`，维护枪口闪光、弹道、命中粒子等短生命周期表现状态。
  事件消费现在还会登记到固定容量的 `rasterfall_effect_instance` runtime 池；旧的 muzzle/tracer/
  particle 池继续负责现有绘制，因此本阶段不改变画面。`EXPLOSION` 已有 runtime 类型和生命周期，
  暂无 emitter 或视觉绘制。事件类型统一定义在 `include/rasterfall_effect_event.h`，该模块不反写 gameplay。
- `rasterfall_sky.c`：天空背景。
- `rasterfall_perf.c`：阶段计时、场景统计和性能输出。
- `src/dev-tests/*.inc`：角色基准和蒙皮跟踪，直接包含进 render 编译单元。

## 一帧的数据流

主循环更新 session/net/effects 后，设置 `rasterfall_render_context`，调用场景及实体公开入口，
底层 renderer 收集/光栅化几何；随后绘制 HUD、菜单和调试叠层并 present。客户端角色展示可能使用
网络插值状态，不应误读为权威 `toy_game` 状态。

战斗事件链路为：规则结果/网络展示适配器 → `rasterfall_effect_event` →
`rasterfall_effects_consume()` → runtime instance pool，同时旁路进入现有 muzzle/tracer/particle
池。runtime instance 统一拥有类型、位置、方向、速度、生命周期/年龄、尺寸和 alpha 等基础状态；
更新阶段统一推进其运动和寿命，但当前渲染仍使用旧池。射击同步器只搬运规则层射线与枪口坐标；
伤害、命中规则和网络快照不读取或写入这些视觉状态。

下一阶段接入爆炸时，应由炸弹命中/结束位置产生 `RASTERFALL_EFFECT_EVENT_EXPLOSION`，在
`rasterfall_effects_consume()` 中把一个 explosion instance 扩展为固定数量的子 instance（或由
独立 emitter 状态按时间生成），再由 `rasterfall_render_particles()` 增加 explosion 类型的绘制分支。
该接入不应修改炸弹伤害和网络协议；联机事件仍应由展示适配器构造已有 event。

## 常见任务落点

- 世界物体缺失或遮挡错误：`render_scene()`、对应 `render_*`，再查近裁剪和 depth 路径。
- 角色模型/LOD/并行渲染：character loading、`render_characters_parallel()`、骨骼角色入口。
- 第一人称枪械位置或枪口：`rasterfall_viewmodel.c`；第三人称持枪在 render/model/calibration。
- UI、记分板、伤害闪屏：`rasterfall_hud.c` 或 `rasterfall.c` 中独立 overlay。
- 光照、纹理、材质：lightmap 和 textured triangle 路径；资产解码在 `lib/assets.c`。
- 性能回归：先用 `rasterfall_perf` 的分阶段数据区分玩法、建模、提交和 raster，再改实现。

模型和动画的求值边界见 [assets-animation.md](assets-animation.md)。改可见结果时保留确定性截图/像素
测试的价值；改并行路径时还要比较单 worker 和多 worker 的画面与统计。
