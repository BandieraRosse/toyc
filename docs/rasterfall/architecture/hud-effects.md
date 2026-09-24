# HUD、Viewmodel 与特效

> 状态：当前
> 所有者：Rasterfall presentation-only HUD/effects runtime
> 最近核对：2026-09-23

本文定义 HUD、第一人称 viewmodel 和短生命周期特效的所有权与层序。帧 barrier 和 target 语义见
[渲染架构](rendering-architecture.md)，玩法事件与网络真值由 gameplay/session/network 层拥有。

## 模块与层

独立 Scene 路径不调用旧整帧 producer。`rasterfall_canvas` 将布局变成裁剪后的矩形与
UTF-8 点阵字形 run；`rasterfall_sky_layout`、`rasterfall_hud_layout` 和 prompt layout
不持有 surface。旧入口使用 surface adapter，新入口直接生成 SKY/OVERLAY GPU 三角形，
不存在 CPU HUD 图片上传或 mixed recording。

暂停设置、结算、武器散布准星和 Tab 计分板也使用同一 canvas 布局。runtime 通过
同步 `ui_layout` 回调提供只读菜单状态，Scene 在资源准备前将其冻结为 OVERLAY 几何；
回调不保存到异步 GPU slot。AI 名字、血量和倒地/救援进度由 Scene 从当前 actor 展示值生成。
正常运行的 Desktop/Console 仍遵守 [Application Runtime](application-runtime.md) 的关闭 gate。

`rasterfall_viewmodel_geometry` 共享手臂、武器、换弹、摆动与 local muzzle 的几何求值，
通过显式 callback 输出投影顶点、材质 alpha、光照及纹理。旧 renderer adapter 和 Scene
adapter 分别消费，几何入口不创建 RasterCmd。Scene 屏幕顶点保留 inverse-Z 与透视 UV，
VIEWMODEL 深度域及覆盖合成由 [GPU 架构](gpu-rendering-architecture.md)拥有。

独立特效 adapter 只读固定容量 instance 池，在提交前生成 EFFECTS 几何；本地枪口留在
VIEWMODEL，screen overlay 留在 OVERLAY。首版表现的未完成项由活动计划跟踪。
Scene 射线使用世界空间 ribbon，由硬件执行近裁剪与透视；死亡碎片使用三维盒体，
受击箭头消费 effect instance 中已经量化的方向，不重新计算玩法伤害。

- `rasterfall_hud.c`：玩家、网络、波次、商店、交互提示、菜单以及 BMP/帧导出。
- `rasterfall_viewmodel.c`：第一人称手臂、武器、摆动/后座和 local muzzle placement。
- `rasterfall_effects.c`：消费 effect event，更新固定容量 runtime instance/emitter 池并提交可见组件。
- `rasterfall_sky.c`：SKY 层；不属于 HUD 或 screen overlay。

world EFFECTS 与 VIEWMODEL 位于 Post/OVERLAY 之前，参与场景 depth；screen overlay 位于 Core 明确的
overlay barrier 之后。prompt、名字/状态、crosshair、HUD、pause、game-over、scoreboard、input debug、
console 和 GUI desktop 共用 Core-owned color+coverage overlay。不得把不支持的 world effect 或 viewmodel
direct pixels 改称 overlay 来绕过 consumer 缺口。

## 事件与 runtime

数据流固定为：

```text
gameplay result / network presentation adapter
    -> rasterfall_effect_event
    -> rasterfall_effects_consume()
    -> fixed-capacity instance/emitter pool
    -> world EFFECTS / VIEWMODEL / OVERLAY submission
```

instance 将组件类型（particle、ray、billboard、overlay、emitter、material、camera shake）与语义 kind
分离，并持有位置、方向、速度、年龄/寿命、尺寸和 alpha。固定 16ms update 推进视觉状态；池满时按写指针
覆盖最旧槽位。玩法伤害、命中规则和网络快照不读取这些状态。

tracer、命中火花、muzzle flash、爆炸和 Molotov 分别由 ray/particle/billboard/emitter 组合表达。
本地 muzzle core/outer/lobe 使用 VIEWMODEL projection、独立 depth 与 coverage；remote/AI muzzle 留在
world EFFECTS。透明 child 使用 source-over、depth-test/no-depth-write，不上传成 screen overlay。

explosion 和 fire zone 只从 preset 复制固定 emitter 描述，事件填充位置等动态字段。enemy death fragment、
dust 与 dissolve 使用有序透明 EFFECTS command；生命周期可以越过 gameplay slot 清理，但不反写 Game。

## Camera 与反馈

camera shake 只作用于渲染阶段复制的 `render_camera`。多个组件按轴叠加、限幅，再做短时平滑；本地开火
由 LOCAL_VIEW 标志隔离，AI/远端事件不得改变本地镜头。受击 shake 使用独立 preset 和最短接受间隔，
reset 必须清除未完成方向。

早期 200% 射速下的 camera recoil 调参表见[历史记录](../archive/hud-recoil-tuning-2026-09.md)；当前数值以 `rasterfall_effects.c` 与相关配置常量为准。

`DAMAGE_FLASH` 在 overlay 绘制低透明红边和方向箭头。方向可由展示层估计最近存活敌人并量化为八方向，
但该估计不能进入 `toy_game` 或 snapshot。enemy material hit feedback 与 screen damage overlay 是不同组件。

tracer 保留事件的起点和终点，渲染时按年龄截取移动短段。local tracer 可按相机深度抑制枪口附近亮度；
AI/远端使用较短世界空间方柱，避免固定屏幕宽度破坏透视。weapon presentation descriptor 可以提供颜色、
线宽和寿命，但不影响射击规则。

## HUD 数据来源

HUD 与 viewmodel 从 player actor 及 session/network presentation state 读取。远端连接状态来自 client，
位置/朝向可以来自插值 cache；生命、武器、downed 和统计仍来自对应 actor。renderer/HUD 不缓存或修改
权威 gameplay 真值。

内嵌 8×16 VGA ASCII 与 16×16 GB2312 字形由项目资产提供；运行时不依赖 FreeType、系统 CJK 字体或
宿主编码转换。地图排布导出可以读取同一字形资产，但不拥有 HUD runtime。

验证矩阵见 [视觉验收](../guides/visual-validation.md)和[GPU 验收与诊断](../guides/gpu-validation.md)。
