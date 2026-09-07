# Rasterfall 代码导航

> 文档更新：2026-09-07
> 源码核对基线：工作区（地图静态 prop 实例解析、profile 驱动 RFU 碰撞 primitive 与渲染遍历；`rasterfall_prop` 静态资产 profile 和 world-space RMESH prop 渲染入口；本地玩家、远端人类、AI 和 special controller 的 gameplay 状态统一由 `toy_game_actor` 拥有）

本目录面向接手 Rasterfall 任务的编码代理。目标不是介绍玩法，而是先把问题归到正确的
状态所有者和文件，再开始搜索。命令、资源导入方法和用户可见特性仍以
[`../README.md`](../README.md) 为准。

## 先读哪一篇

| 任务或症状 | 首先阅读 | 主要入口 |
| --- | --- | --- |
| 启动、参数、输入、暂停、主循环、音画同步 | [runtime.md](runtime.md) | `src/rasterfall.c` |
| 武器、敌人、碰撞、寻路、波次、商店、AI | [gameplay.md](gameplay.md) | `lib/game.c`、`src/rasterfall_session.c` |
| 地图格式、关卡实体、拾取物、静态 prop、出生点 | [map-format.md](map-format.md) | `lib/map.c`、`src/rasterfall_map.c` |
| 编写或扩展 `.map` 文本格式 | [map-format.md](map-format.md) | `lib/map.c`、`include/toy_map.h` |
| 场景、角色、HUD、特效、第一人称武器、性能 | [rendering.md](rendering.md) | `src/rasterfall_render.c` |
| world-space 静态 RMESH prop、实例变换和开发展示 | [rendering.md](rendering.md) | `include/rasterfall_render.h`、`src/rasterfall_render.c` |
| 战斗表现事件、muzzle/tracer/impact/camera shake 消费 | [rendering.md](rendering.md) | `include/rasterfall_effect_event.h`、`src/rasterfall_effects.c` |
| 模型、蒙皮、IK、VMD/GLB、动作重定向 | [assets-animation.md](assets-animation.md) | `src/rasterfall_model.c` |
| 转换 PMX/GLB、生成 LOD、模型诊断 | [asset-pipeline.md](asset-pipeline.md) | `app/`、`tools/`、模型加载器 |
| 程序化工业/军事环境组件、Blender 批量导出 | [industrial-props.md](industrial-props.md)、[asset-pipeline.md](asset-pipeline.md) | `tools/blender/generate_rasterfall_props.py` |
| 静态 prop 资产 ID、路径、展示缩放和默认尺寸 | [asset-pipeline.md](asset-pipeline.md) | `include/rasterfall_prop.h`、`src/rasterfall_prop.c` |
| 联机协议、快照、预测、可靠事件、房间发现 | [networking.md](networking.md) | `src/rasterfall_net.c` |
| Linux/Windows 平台差异、构建、测试 | [build-platforms.md](build-platforms.md) | `Makefile`、`windows/Makefile` |
| 动画求值顺序、格式/角色扩展契约 | [animation-architecture.md](animation-architecture.md) | `src/rasterfall_model.c`、动画头文件 |
| 网络状态所有权、协议和房间生命周期 | [network-architecture.md](network-architecture.md) | `src/rasterfall_net.c`、公共协议头 |
| 资源来源、许可和发布检查 | [asset-sources.md](asset-sources.md) | 资源目录与导入工具 |

## 架构主线

```text
平台事件/网络包
      ↓
src/rasterfall.c                 进程、菜单、输入、固定步长帧循环
      ↓
src/rasterfall_session.c         单机/主机/客户端会话编排与 controller
      ↓
actor API → toy_game_actor       各类角色共享的状态与动作入口
      ↓
lib/game.c                       world simulation 与确定性玩法规则
      ↓
src/rasterfall_render.c + HUD    只读玩法状态并生成画面
      ↓
toy_renderer / window / audio    仓库公共平台层
```

战斗表现事件是 presentation-only 数据，统一经 `rasterfall_effects` 消费为短生命周期表现状态，
并登记到固定容量的 `rasterfall_effect_instance` runtime 池；instance 将底层组件类型与效果语义
分离，tracer、命中火花、分层 muzzle flash 和 Molotov 火焰已由 runtime `RAY`/`PARTICLE`/`BILLBOARD` 组件绘制；屏幕空间反馈已有 `OVERLAY` 原语入口，主循环通过统一 `rasterfall_render_effects()` facade 消费这些 instance。FIRE 与 EXPLOSION 通过统一 emitter preset table 描述生命周期、生成间隔和固定 child descriptor 列表。它不进入
受击时由展示态生命值下降触发本地 `CAMERA_SHAKE` preset；其参数和最短间隔位于
`include/rasterfall_effects.h`。它不进入 `toy_game` 的权威状态同步。

网络主机运行权威会话；客户端通过 `rasterfall_net.c` 的快照、预测与校正形成展示状态。炸弹和
Molotov 的世界实体显式携带 `owner_actor_id`，本地预测 body 位置再派生 camera 展示位置。
`struct camera` 已显式区分 `body` 与 `view` 命名空间；扁平字段仅作为迁移期布局兼容别名。
主机和客户端的开火展示都经 `sync_network_fire_effects()` 适配到同一 runtime，并按 fire sequence
抑制重复事件。不要把纯视觉状态塞进 `toy_game`，也不要让渲染器修改权威玩法结果。

## 目录边界

- `rasterfall/include/`：模块公开结构、枚举和函数契约；定位状态所有权时先看对应头文件。
- `rasterfall/lib/`：可脱离窗口和渲染验证的玩法、地图解析和声音合成核心。
- `rasterfall/src/`：应用编排、网络、渲染、资产运行时和界面。
- `rasterfall/assets/`：公开运行时地图、纹理、音效和模型。
- `rasterfall/private-assets/`：可选本地资产；代码不能假设每个工作区都有它。
- `app/`、`lib/`、`include/`、`windows/`、`tools/` 中也有 Rasterfall 使用的转换器与平台层，
  详见各模块文档。

专题设计和活动台账：

- [industrial-props.md](industrial-props.md)：首批十件环境组件规格、米制轴向、碰撞建议与一条命令生成流程；源码核对为 2026-09-07 工作区生成器及静态转换器。

- [animation-architecture.md](animation-architecture.md)：动画数据流、不变量和扩展边界。
- [network-architecture.md](network-architecture.md)：联机状态分类和房间生命周期。
- [asset-sources.md](asset-sources.md)：资源身份、许可状态和发布前检查。
- [asset-pipeline.md](asset-pipeline.md)：资产转换、检查器、LOD 和离屏回归命令。
- [map-format.md](map-format.md)：地图文本语义、示例和跨层修改要求。

历史资料放在 `archive/`。其中的结论只描述当时现场，不是当前设计依据；除非任务明确要求追溯
历史，不要先阅读归档文档。

## 修改时的定位原则

先找“谁拥有状态”，再找“谁展示状态”。改公共结构时同时搜索其序列化、测试和所有调用者。
尤其注意以下跨层联动：

- 修改 `toy_game.h` 的武器、敌人、事件或结构布局：检查 `lib/game.c`、session、HUD、渲染和网络编码。
- 修改地图语义：检查 `toy_map.h`/`lib/map.c` 的解析、`rasterfall_map.c` 的绑定、玩法碰撞和渲染。
- 修改角色动画：检查角色选择、会话动画状态、模型求值、渲染以及网络动画字段。
- 修改敌人外观组件：检查 `src/rasterfall_render.c` 的 `enemy_body_part` 描述表、通用组件解释器和特感动态组件；地面锚点仍由 `toy_game_enemy.ground_y` 与 `airborne_y` 提供。
- 修改命令行或诊断模式：从 `rasterfall_options.c` 到 `rasterfall.c` 的早退分支一起核对。
- 修改角色状态：从 `toy_game_actor`、actor API 和
  `rasterfall_session.c` 的本地主循环开始；不要把 camera 的位置字段写回为 gameplay 源。

## 修改后更新哪些文档

| 改动 | 必须同步检查 |
| --- | --- |
| 模块拆分、状态所有权或主数据流变化 | 本页及所有受影响模块导航 |
| 动画格式、姿态顺序、角色扩展机制 | `assets-animation.md`、`animation-architecture.md` |
| 资产格式、转换器、LOD 或诊断入口 | `asset-pipeline.md`、`assets-animation.md` |
| 地图记录、属性或绑定语义 | `map-format.md`、`gameplay.md` |
| 协议字段、权威规则、房间生命周期 | `networking.md`、`network-architecture.md` |
| 构建目标、平台源文件或资源打包 | `build-platforms.md`、用户 `../README.md` |
| 资源来源、许可或发布范围 | `asset-sources.md` |

更新正文时同时刷新顶部“文档更新”和“源码核对基线”。重大重构、重要文件迁移或重大功能变动
必须在同一改动中更新本索引，确保下一位维护者在阅读源码前得到正确入口。
