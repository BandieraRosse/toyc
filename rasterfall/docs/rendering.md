# 渲染、HUD、特效与性能

> 文档更新：2026-09-05
> 源码核对基线：工作区（RAY tracer 短线段/定向线宽投影，通用 emitter preset table，CAMERA_SHAKE 含开火后座与受击摇晃）

## 渲染边界

`src/rasterfall_render.c` 是世界渲染和角色渲染主体：投影/近裁剪、三角形提交、地面与地图图元、
拾取物、敌人、玩家/队友、骨骼角色、弹道粒子及模型诊断。公开入口在
`include/rasterfall_render.h`，共享状态由 `rasterfall_render_context` 绑定。

`src/render/rasterfall_render_frontend.c` 是渲染器前端适配，管理默认纹理、覆盖配置和 worker
绑定；底层光栅器在仓库公共的 `lib/graphics/renderer.c` / `include/toy_renderer.h`。

其他视觉模块：

- `rasterfall_hud.c`：玩家、网络、波次、商店 HUD，交互提示和 BMP/帧导出。
- `rasterfall_viewmodel.c`：第一人称手臂、武器模型、后坐/摆动和枪口位置。
- `rasterfall_effects.c`：消费 `rasterfall_effect_event`，并从投掷物/燃烧区域展示状态同步枪口闪光、弹道、命中粒子、炸弹闪烁、Molotov 火焰和局部镜头晃动等短生命周期表现状态。
  事件消费现在还会登记到固定容量的 `rasterfall_effect_instance` runtime 池；instance 将底层
  组件类型（particle/ray/billboard/overlay/emitter/material/camera_shake）与语义 kind 分离。tracer、命中火花、分层 muzzle flash 和 Molotov 火焰已迁移到统一
  `RAY`/`PARTICLE`/`BILLBOARD` 组件；`ENTITY_HIT` 生成命中粒子但不再额外生成整条 hit ray，炸弹 fuse flash 使用 billboard；玩家伤害闪屏使用 `OVERLAY`，敌人受击颜色使用 `MATERIAL` feedback，交互高亮已登记为短生命周期 `INTERACTION_HIGHLIGHT` billboard 并驱动现有高亮绘制，屏幕空间效果通过 `render_effect_overlay()` 和
  `rasterfall_render_overlays()` 提供统一入口，因此本阶段不改变已有效果画面。`CAMERA_SHAKE`
  组件不修改权威摄像机，只在渲染阶段复制出的 `render_camera` 上叠加视空间平移、偏航和俯仰扰动；当前仅本地 `WEAPON_FIRE` 事件生成该组件，AI/远端开火事件通过 `LOCAL_VIEW` 标志隔离。多个组件先按轴叠加，再按每轴最大值限幅，并用短时插值追踪目标值。`EXPLOSION`
  现在由固定生命周期的 emitter 生成 16 个通用 `EXPLOSION_PARTICLE` 子实例；事件类型统一定义在
  `include/rasterfall_effect_event.h`，该模块不反写 gameplay。
- `rasterfall_sky.c`：天空背景。
- `rasterfall_perf.c`：阶段计时、场景统计和性能输出。
- `src/dev-tests/*.inc`：角色基准和蒙皮跟踪，直接包含进 render 编译单元。

## 一帧的数据流

主循环更新 session/net/effects 后，设置 `rasterfall_render_context`，调用场景及实体公开入口，
底层 renderer 收集/光栅化几何；随后绘制 HUD、菜单和调试叠层并 present。客户端角色展示可能使用
网络插值状态，不应误读为权威 `toy_game` 状态。

战斗事件链路为：规则结果/网络展示适配器 → `rasterfall_effect_event` →
`rasterfall_effects_consume()` → runtime instance pool。tracer、命中火花、
muzzle flash 和 Molotov 火焰的渲染已经直接消费 runtime `RAY`/`PARTICLE`/`BILLBOARD` instance；runtime instance 统一拥有组件类型、
语义 kind、位置、方向、速度、生命周期/年龄、尺寸和 alpha 等基础状态。更新阶段统一按固定 16ms
步进推进粒子运动和寿命；
射击同步器只搬运规则层射线与枪口坐标；
伤害、命中规则和网络快照不读取或写入这些视觉状态。镜头晃动实例在固定步中先按轴叠加并限幅，
再以 `RASTERFALL_CAMERA_SHAKE_SMOOTHING` 对聚合目标做短时插值；快速连射因此会累积到上限，
停火后平滑回零。

当前镜头后座平衡参考（玩家射速倍率 200%，实际间隔为基础 cooldown 的一半）如下：

| 武器 | 实际间隔 | 后座衰减 | 垂直单发振幅 | 理论连续峰值 | 垂直上限 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 手枪 | 100 ms | 110 ms | 8 | 约 9 | 18 |
| SMG | 50 ms | 150 ms | 8 | 约 16 | 20 |
| AK | 90 ms | 240 ms | 18 | 约 32（触顶） | 32 |
| 霰弹枪 | 400 ms | 170 ms | 16 | 约 16（单发） | 28 |
| AWP | 600 ms | 110 ms | 5 | 约 5（单发） | 10 |

理论连续峰值按“同向叠加、线性衰减”估算，实际画面还会经过短时插值和确定性噪声，因此会略低或有小幅波动。

爆炸由炸弹命中/结束位置产生 `RASTERFALL_EFFECT_EVENT_EXPLOSION`，在
`rasterfall_effects_consume()` 中登记一个固定容量 emitter，并由 emitter 按间隔把子组件写入统一
instance pool；当前爆炸配置生成冲击波 ray、短时 billboard 和固定数量的粒子子 instance，由
  `rasterfall_render_effects()` 的通用 world primitive 分支绘制。该接入不修改炸弹伤害和网络协议；联机事件仍应由展示适配器构造已有 event。当前 Molotov 火焰已经通过
`rasterfall_effects_sync_fire_zones()` 将每个燃烧区域同步为固定容量 emitter；emitter 按固定间隔批量生成
  `FIRE` 语义的 `PARTICLE` instance，并由通用粒子绘制入口消费。emitter 的子组件类型、生成间隔、
  数量上限、散布和 placement pattern 都是固定容量 runtime 描述；FIRE 与 EXPLOSION 都从统一
  preset table 复制 emitter 标量参数及 child descriptor 列表，事件只负责填充位置等动态字段，主循环现在通过
  `rasterfall_render_effects()` 统一提交 ray/billboard/particle，overlay 仍在屏幕空间阶段单独提交，
  以保证 HUD 和第一人称视图模型的层级顺序。
  镜头晃动不进入世界 primitive 绘制；`rasterfall_effects_apply_camera_shake()` 在世界渲染前对当前
  `render_camera` 做确定性衰减采样。受击摇晃使用独立的可配置 preset：默认随机左右偏航约 15°，
  先快速到峰值、短暂保持，再在 500ms 内连续衰减；重置时清除未完成的受击方向。最短接受间隔内
  的重复伤害不会生成新的摇晃，参数位于 `include/rasterfall_effects.h`。
  统一 instance 和 emitter 池均为固定容量环形池，满载时按写指针覆盖最旧槽位；该策略已由逻辑测试覆盖。后续可在不改变火焰语义的前提下替换粒子渲染细节。

tracer RAY instance 保留事件提供的枪口起点和命中/射程终点，但绘制时按 lifetime/age 在两点
之间截取短段并推进到终点，不再显示整条弹道。颜色在整条可见短段内保持统一，线宽、尾段比例和 68--86ms 的寿命
由 `toy_game_weapon_info` 的 presentation-only descriptor 提供；普通武器统一白色，AK/AWP
采用黄橙色。第一人称本地玩家 tracer 按相机空间深度抑制枪口近处亮度，使用平滑插值从约中
距离开始逐渐显现；AI/远端玩家也使用短线段快速移动，但采用更短的可见段和独立距离参数。
AI/远端 tracer 使用世界空间小方柱，避免沿射线方向观察时固定屏幕线宽盖过透视长度而显示为横线。
instance pool、事件和深度测试 flags 不变。

## 常见任务落点

- 世界物体缺失或遮挡错误：`render_scene()`、对应 `render_*`，再查近裁剪和 depth 路径。
- 角色模型/LOD/并行渲染：character loading、`render_characters_parallel()`、骨骼角色入口。
- 第一人称枪械位置或枪口：`rasterfall_viewmodel.c`；第三人称持枪在 render/model/calibration。
- UI、记分板、伤害闪屏：`rasterfall_hud.c` 或 `rasterfall.c` 中独立 overlay。
- 光照、纹理、材质：lightmap 和 textured triangle 路径；资产解码在 `lib/assets.c`。
- 性能回归：先用 `rasterfall_perf` 的分阶段数据区分玩法、建模、提交和 raster，再改实现。

模型和动画的求值边界见 [assets-animation.md](assets-animation.md)。改可见结果时保留确定性截图/像素
测试的价值；改并行路径时还要比较单 worker 和多 worker 的画面与统计。
