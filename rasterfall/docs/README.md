# Rasterfall 代码导航

> 文档更新：2026-09-09
> 源码核对基线：工作区（RFCHAR GLB → RFM2 v14/SKN1/CHR1 → stable API/CPU skinning；RF Humanoid V1.1 保留为对照，V2 proportion/silhouette rebuild 已接入同一 Character Acceptance 与可选 world capture；其余主线同现有工作区）

本目录面向接手 Rasterfall 任务的编码代理。目标不是介绍玩法，而是先把问题归到正确的
状态所有者和文件，再开始搜索。命令、资源导入方法和用户可见特性仍以
[`../README.md`](../README.md) 为准。

文档职责分层如下：根目录 `README.md` 是仓库总览，`rasterfall/README.md` 是稳定的用户入口，
本目录是维护者导航和模块细节。易变的参数完整清单以 `build/rasterfall --help` 为准；可复核的
玩法、资产、模型和地图事实优先通过本仓库提供的 CLI 入口获取，详见根目录 `AGENTS.md` 的
“文档层级与 Agent CLI 事实入口”。

> 源码核对补充：正式 Hurd 四人使用专用 character IDs；原 Maid 四人旗卫已在西侧原位恢复并使用 Maid character/profession；普通 player、Eula 和佣兵为 character NONE；北侧 HURD 旗帜及派生 control status 已接入 session。

## 先读哪一篇

| 任务或症状 | 首先阅读 | 主要入口 |
| --- | --- | --- |
| 启动、参数、输入、暂停、主循环、音画同步 | [runtime.md](runtime.md) | `src/rasterfall.c` |
| agent 固定视觉场景截图 / Visual CLI | [rendering.md](rendering.md)、[runtime.md](runtime.md) | options → `rasterfall_render_visual_capture()` → `src/dev-tests/rasterfall_visual_capture.inc` |
| 武器、敌人、碰撞、寻路、波次、商店、AI | [gameplay.md](gameplay.md) | `lib/game.c`、`src/rasterfall_session.c` |
| Hurd 固定小队、北侧据点、旗帜控制真值 | [gameplay.md](gameplay.md)、[map-format.md](map-format.md) | `rasterfall_session.h` 的 Hurd config/status → `rasterfall_session_hurd_status()` |
| 地图格式、关卡实体、拾取物、静态 prop、出生点 | [map-format.md](map-format.md) | `lib/map.c`、`src/rasterfall_map.c` |
| 编写或扩展 `.map` 文本格式 | [map-format.md](map-format.md) | `lib/map.c`、`include/toy_map.h` |
| 修改地图排布、导出地图俯视图、agent 可读 JSON 和精确布局查询 | [map-format.md](map-format.md) | `tools/map_layout_export.py`、`tools/map_layout_query.py`、`make map-layout` |
| 场景、角色、HUD、特效、第一人称武器、性能 | [rendering.md](rendering.md) | `src/rasterfall_render.c`、`src/dev-tests/rasterfall_visual_capture.inc` |
| Hurd 职业外观、低模 AI 人体、RF Humanoid V1/V2、基础外观、指定角色独立绘制入口 | [rendering.md](rendering.md) | `rasterfall_render.h` 的 `rasterfall_procedural_humanoid_state` / `rasterfall_render_procedural_humanoid()`；`rasterfall_character.h` 的基础/职业 profile；`dev-tests/rasterfall_visual_capture.inc` 的角色验收与 world strip |
| 中文 UI、UTF-8 文本和 GB2312 点阵字库 | [rendering.md](rendering.md)、[asset-sources.md](asset-sources.md) | `lib/graphics/fb_font.c`、`assets/fonts/` |
| world-space 静态 RMESH prop、实例变换和开发展示 | [rendering.md](rendering.md) | `include/rasterfall_render.h`、`src/rasterfall_render.c` |
| 战斗表现事件、muzzle/tracer/impact/camera shake 消费 | [rendering.md](rendering.md) | `include/rasterfall_effect_event.h`、`src/rasterfall_effects.c` |
| 模型、蒙皮、IK、VMD/GLB、动作重定向 | [assets-animation.md](assets-animation.md) | `src/rasterfall_model.c` |
| Blender 人形角色、RF Humanoid、Character GLB、附件、导入与蒙皮门禁 | [character-assets.md](character-assets.md) | `tools/assets/rfchar_import.py`、`include/rasterfall_model.h`、`app/glb_inspect.c`、`dev-tests/rasterfall_visual_capture.inc` |
| 导入 PMX/GLB、manifest、纹理、LOD、模型诊断 | [asset-pipeline.md](asset-pipeline.md) | `tools/assets/import_asset.py`、现有转换器、模型加载器 |
| 程序化工业/军事环境组件、Blender 批量导出 | [industrial-props.md](industrial-props.md)、[asset-pipeline.md](asset-pipeline.md) | `tools/blender/generate_rasterfall_props.py` |
| 环境组件 V2 风格、palette、几何/纹理预算与验收 | [environment-art.md](environment-art.md)、[industrial-props.md](industrial-props.md) | 十件 static prop 源资产与游戏内展示 |
| 整套 V2 Hybrid 生成、flat/局部 sign 分配 | [industrial-props.md](industrial-props.md)、[asset-pipeline.md](asset-pipeline.md) | 生成器 `PILOT`、`ACCENTS`、`Builder.box()`、`hybrid_prop()`；crate 沿用 `hybrid_crate()` |
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
受击时由展示态生命值下降触发本地 `CAMERA_SHAKE` preset；同一展示同步入口生成浅色四角边缘
闪红，并根据最近存活敌人与相机朝向生成八方向中心受击箭头。其参数和最短间隔位于
`include/rasterfall_effects.h`。它不进入 `toy_game` 的权威状态同步。

网络主机运行权威会话；客户端通过 `rasterfall_net.c` 的快照、预测与校正形成展示状态。炸弹和
Molotov 的世界实体显式携带 `owner_actor_id`，本地预测 body 位置再派生 camera 展示位置。
`struct camera` 已显式区分 `body` 与 `view` 命名空间；扁平字段仅作为迁移期布局兼容别名。
主机和客户端的开火展示都经 `sync_network_fire_effects()` 适配到同一 runtime，并按 fire sequence
抑制重复事件。不要把纯视觉状态塞进 `toy_game`，也不要让渲染器修改权威玩法结果。

player/actor 和敌人的 airborne forced/knockback movement 均由玩法核心按各自碰撞半径分段扫掠；每个子步
保留 X/Z 分轴滑动，并在受阻时清除对应 knockback 分量。不可高抛越过的地图边界使用 primitive
的显式 `BLOCKS_AIRBORNE` 属性，不从 `role` 推导。

## 目录边界

- `rasterfall/include/`：模块公开结构、枚举和函数契约；定位状态所有权时先看对应头文件。
- `rasterfall/lib/`：可脱离窗口和渲染验证的玩法、地图解析和声音合成核心。
- `rasterfall/src/`：应用编排、网络、渲染、资产运行时和界面。
- `rasterfall/assets/`：公开运行时地图、纹理、音效和模型。
- `rasterfall/private-assets/`：可选本地资产；代码不能假设每个工作区都有它。
- `app/`、`lib/`、`include/`、`windows/`、`tools/` 中也有 Rasterfall 使用的转换器与平台层，
  详见各模块文档。

专题设计和活动台账：

- [industrial-props.md](industrial-props.md)：十件 V2 Hybrid 规格、flat/decal 分工、米制轴向、碰撞建议与生成/统一导入流程。
- [environment-art.md](environment-art.md)：十件工业 / 军事组件的 V2 light upgrade 风格、逐件要点、预算、验收与试点顺序。

- [animation-architecture.md](animation-architecture.md)：动画数据流、不变量和扩展边界。
- [character-assets.md](character-assets.md)：RF Humanoid V1、Character GLB、attachment 与 skinning 的冻结契约和 validator 门禁。
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
- 修改离线资产导入契约：从 `tools/assets/import_asset.py` 和 manifest 开始，分别检查
  `glb2rmesh`/`pmx2rmesh`、`toyasset`、`rmesh_lod.py`；不要让 runtime 读取 manifest。
- 修改敌人外观组件：检查 `src/rasterfall_render.c` 的 `enemy_body_part` 描述表、通用组件解释器和特感动态组件；地面锚点仍由 `toy_game_enemy.ground_y` 与 `airborne_y` 提供。
- 修改命令行或诊断模式：从 `rasterfall_options.c` 到 `rasterfall.c` 的早退分支一起核对。
- 修改职业外观：四个 Hurd profile 与 Maid profile 保存 profession identity；普通 player、Eula、佣兵的 `character_id` 为 NONE。Maid 旗卫仍由 `anime_character_id` 选择各自骨骼模型，同时由 `character_id` 标识共同 Maid 职业。静态 presentation profile 保存附件配置；actor 展示适配器从 `character_id` 解析职业。actor 和网络不携带重复的职业或附件字段；Visual CLI 提供固定 Hurd 小队。
- 修改角色状态：从 `toy_game_actor`、actor API 和
  `rasterfall_session.c` 的本地主循环开始；不要把 camera 的位置字段写回为 gameplay 源。
- 修改 Hurd 据点：固定角色索引和 RFU control region 属于 session；assignment 只读
  `actor.flag_index`，capable 只认 ALIVE 且 HP 大于零；旗帜、actor 和 revive 仍是底层玩法真值。

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
