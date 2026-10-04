# HUD、Viewmodel 与特效

> 状态：当前
> 所有者：Rasterfall presentation-only HUD/effects runtime
> 最近核对：2026-10-04

本文定义 HUD、第一人称 viewmodel 和短生命周期特效的所有权与层序。帧 barrier 和 target 语义见
[渲染架构](rendering-architecture.md)，玩法事件与网络真值由 gameplay/session/network 层拥有。

## 模块与层

独立 Scene 路径不调用旧整帧 producer。`rasterfall_canvas` 将布局变成裁剪后的矩形与
UTF-8 点阵字形 run；`rasterfall_sky_layout`、`rasterfall_hud_layout` 和 prompt layout
不持有 surface。旧入口使用 surface adapter，新入口直接生成 SKY/OVERLAY GPU 三角形，
不存在 CPU HUD 图片上传或 mixed recording。正常帧的暂停、结算、准星、计分板和 RTS 控件由
`rf_game_shared_ui_layout` 同步决定；CPU 用 surface canvas 消费，独立 Scene 用三角形 canvas 消费。

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

Combat V0 的玩家生命条读取 actor 的实际 `max_hp`，回避条读取最终能力容量和 actor 的毫点储备；
低储备和耗尽使用警告色，反应、压力等待与就绪状态分别显示。回避反应由权威序号触发青色侧身
碎光，同一序号的重复快照不重新播放，实例 generation 变化清除旧槽的表现记录。回避效果不改变
镜头、位置或伤害结果。射线只有确认生命伤害大于零时才产生身体命中特效，actor 命中不得写入
感染者的受击方向数组；客户端延后确认的身体效果不重播本地枪口、后坐力或弹道。

内嵌 8×16 VGA ASCII 与 16×16 GB2312 字形由项目资产提供；运行时不依赖 FreeType、系统 CJK 字体或
宿主编码转换。地图排布导出可以读取同一字形资产，但不拥有 HUD runtime。

## 玩家界面 V2 的展示契约

`rf_player_ui_state` 由 Game Runtime 持有；`rasterfall_hud_state.player_ui_view` 是本次同步提交的
只读投影。玩家/终端模式使用新的 FPS/RTS HUD，实验与显式 legacy 模式保留原 HUD。
菜单、商店、复活和准星仍消费原有状态，不因界面切换创建新的玩法或设备状态。

- `rf_player_ui.h/c` 拥有语义颜色、720p 设计尺寸、缩放与窗口重排，以及 panel/button/window/text
  基础组件。`rf_ui_layout_resolve` 是 HUD 绘制和命中区域的共同来源；调整主题或布局描述不修改
  生命、弹药、制造、任务或 RTS 命令规则。
- `rf_player_panels.h/c` 拥有设备三栏、收起状态卡、普通/战斗通讯和终端的纯绘制。
  `rf_player_weaver_layout` 与 `rf_player_comms_layout` 同时提供绘制、点击和 GPU 辅助镜头矩形。
  UI 不执行按钮业务；Runtime 根据命中 ID 调用共享命令边界并展示真实查询结果。
  渲染终端和指挥桌复用 `rf_player_device_layout/hit/draw`，分别呈现设备查询的能力行和地图卡片，
  底部反馈读取 `device_service.last`。新布局仅替换展示与命中，实验模式继续使用原界面。
- 设备图像和通讯视频区域是明确的透明洞。四周面板分别发射 canvas 几何，禁止用整窗底板盖住
  已合成的 GPU 图像；无视频时显示连接/不可用状态。工程详情是用户显式打开的预览覆盖层。
- `rasterfall_canvas` 仍只发射矩形字形 run；UTF-8 解码、字形宽度、缩放、换行与省略共享同一
  字体步进，不用字符串字节数估计中文宽度。玩家组件通过 `rf_ui_font` 使用 18 px Noto Sans CJK SC
  派生的独立灰度字形缓存；经典与世界字体保持原契约。热点索引缓存与离线合并矩形避免逐帧栅格化，
  无系统字体依赖或 CPU 全屏纹理。字体来源、OFL 许可、生成与校验见资产目录的字体说明。
- 快捷键标签从 `rf_input_action_label` 查询实际绑定；FPS/RTS HUD 的能力值来自当前 actor。
  RTS 旗帜选择显示其实际成员和移动状态，移动目标标记不代表导航路径。
  FPS/RTS 地图统一锚定左下角，区域名称位于地图上方；FPS 显示附近，RTS 使用较大地图及全图尺度。
  任务卡位于区域名称上方，FPS 玩家状态位于右下角，武器 HUD 叠在状态上方。
  RTS 底栏的展开/收起只改变指令与单位面板的 `rts_collapsed` 展示状态，共用 `dock_toggle` 矩形与命中 ID，
  不取消选中、移动命令或会话状态。
  地图与区域名称在 RTS 底栏收起时继续显示，临时展开地图也保持左下角锚点。
  `modal` 投影使 HUD 与交互提示在独立设备/终端窗口打开时收起；`phase_visible` 由场景决定，
  战斗阶段显示真实存活敌人数，其余相关阶段使用实际倒计时。

### 共享地图服务

`rf_minimap.h/c` 是 FPS/RTS 的唯一地图展示来源。地图底图从 `toy_map.draw` 的可见地面、道路、
墙体和建筑足迹构建，保留静态世界坐标图元，不每帧重新渲染俯视世界。`rf_minimap_prepare` 以地图
指针与 Runtime 提供的地图代际复用底图；同代内容更新调用 `rf_minimap_invalidate`，换代清除旧标记。
地图世界到屏幕、逆变换、旋转、缩放和高差提示均使用同一 `rf_minimap_view`。
`rf_player_ui_map_view` 从 UI 布局和只读玩家投影构造该视图，绘制与 Runtime 地图点击共享它，
避免命中坐标另有一套缩放或边距。

Runtime 每次准备 HUD 时更新存活友军的独立标记集合；actor 标记使用高位 ID 命名空间，业务设备和
任务标记使用其余稳定 ID，可通过 `rf_minimap_marker_set/remove` 显式更新与释放。地形缓存不因
友军移动重建。友军以实际阵营规则过滤，服务不遍历感染者位置，也没有隐藏敌人雷达；将来的敌人标记
必须增加真实感知来源后再扩展。密集友军与设备标记按屏幕单元过滤，重要目标在边缘保留方向提示。
默认北向上、玩家箭头转向；FPS 显示附近，RTS 与展开地图使用整体边界。两种布局使用同一目标和可见性规则。

验证矩阵见 [视觉验收](../guides/visual-validation.md)和[GPU 验收与诊断](../guides/gpu-validation.md)。
