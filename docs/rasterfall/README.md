# Rasterfall 维护者入口

> 状态：当前
> 所有者：Rasterfall
> 事实入口：`windows/NativeCodex.ps1`、`build/rasterfall --help`
> 最近核对：2026-09-23

Rasterfall 是本仓库的长期开发主线。本页只负责把任务导向事实所有者；不记录阶段进度、性能数字或完整
参数表。用户构建和启动说明见 [`../../rasterfall/README.md`](../../rasterfall/README.md)，全仓库文档
规则见 [`../repository/documentation.md`](../repository/documentation.md)。

## 开始任务

| 任务 | 先读 | 主要代码或工具入口 |
| --- | --- | --- |
| 当前优先级、GPU 实施顺序 | [活动计划](plans/README.md) | 计划指向的单一执行文档 |
| Windows 原生环境、package、实机验收 | [Windows Native](guides/windows-native.md)、[构建与平台](guides/build-platforms.md) | `windows/NativeCodex.ps1`、`windows/Makefile` |
| 启动、参数、主循环、Core Host | [运行时架构](architecture/runtime.md) | `src/rasterfall.c`、`src/rf_game_runtime.c`、`src/rf_core_host.c` |
| Desktop、Application、GUI 与只读投影 | [Application Runtime](architecture/application-runtime.md) | feature gate、`rasterfall_app.c`、`rf_application_projection.c` |
| 玩法、session、AI、战斗 | [玩法架构](architecture/gameplay.md) | `lib/game.c`、`src/rasterfall_session.c` |
| 地图格式、Runtime Map、World Content | [地图与世界内容](architecture/maps-and-world-content.md)、[地图格式](reference/map-format.md)、[地图编辑](guides/map-authoring.md) | map parser/runtime、projection adapter、布局工具 |
| 世界渲染与帧分层 | [渲染架构](architecture/rendering-architecture.md) | `src/rasterfall_render.c`、Core layer/flush |
| 静态世界光照与诊断 | [光照架构](architecture/static-world-lighting.md)、[验证指南](guides/static-world-lighting.md) | world-light bake、normal consumer 与诊断 scope |
| 角色、敌人与附件表现 | [角色表现](architecture/character-presentation.md) | character/enemy presentation adapters |
| 敌人资源、姿态与固定截图 | [敌人视觉合同](reference/enemy-visuals.md)、[生成验收](guides/enemy-visuals.md) | 感染体家族、特感刚性 profile 与复现入口 |
| HUD、Viewmodel 与特效 | [HUD 与特效](architecture/hud-effects.md) | `rasterfall_hud.c`、`rasterfall_viewmodel.c`、`rasterfall_effects.c` |
| 视觉验收与渲染性能诊断 | [视觉验收](guides/visual-validation.md)、[性能诊断](guides/rendering-performance.md) | capture CLI、`rasterfall_perf`、离屏 benchmark |
| GPU Draw/Raster/present 架构 | [GPU 渲染架构](architecture/gpu-rendering-architecture.md) | `gpu/`、mixed executor、Core Host |
| GPU Scene 迁移与只读 snapshot | [活动计划](plans/README.md)、[迁移接口](plans/gpu-scene-interface.md) | `src/rf_gpu_scene_identity.c`、`src/rf_gpu_scene_frame.c`、`src/rf_gpu_scene_extract.c`；正常帧仍按 GPU 渲染架构 |
| GPU 验收、诊断与性能采样 | [GPU 验收与诊断](guides/gpu-validation.md)、[GPU 性能标准](reference/gpu-performance-standards.md) | `tools/gpu_*.ps1`、Windows native package |
| 模型、蒙皮、动画求值 | [动画架构](architecture/animation-architecture.md) | `src/rasterfall_model.c`、RFANIM/RFCHAR runtime |
| 资产导入、LOD、检查器 | [资产导入与诊断](guides/asset-pipeline.md) | `tools/assets/`、inspect CLI、离屏诊断 |
| 角色与附件资产合同 | [character-assets.md](reference/character-assets.md) | RFCHAR、RFM2、attachment、skinning validator |
| 人形美术生成与职业外观验收 | [美术验收指南](guides/character-art-acceptance.md) | RF Humanoid V1.1/V2、头部覆盖、职业组图 |
| 联机协议、快照、预测与测试 | [联机架构](architecture/network-architecture.md)、[网络测试](guides/network-testing.md) | `src/rasterfall_net.c` |
| 资源来源、许可、发布 | [资源来源台账](reference/asset-sources.md) | 资源台账和发布前检查 |
| 环境资产美术约束与工业组件 | [环境资产艺术约束](reference/environment-art.md)、[工业组件规格](reference/industrial-props.md)、[生成指南](guides/industrial-props.md) | 调色、轮廓、预算、规格与生成入口 |
| 建筑套件、管线端口和墙地表面 | [建筑套件规范](reference/architectural-environment-v1.md)、[生成指南](guides/architectural-environment-v1.md) | 建筑资产、连接合同与复现入口 |
| 临时校园套件与合成场景 | [校园套件规格](reference/temporary-campus-kit-v0.md)、[生成验收](guides/temporary-campus-kit-v0.md) | 视觉资产、拼接与离屏复现；历史审阅见 [阶段记录](archive/temporary-campus-kit-v0.md) |
| 武大资料、真实空间依据 | [reference/return-to-whu-core/](reference/return-to-whu-core/) | 来源台账、调查报告、白盒计划 |
| 已完成或撤销的现场 | [archive/](archive/) | 只作历史证据，不作当前设计依据 |

## 可执行事实

优先以这些入口取得当前事实：

- `build/rasterfall --help`：运行参数的完整清单。
- `build/rasterfall --logic-test`：玩法、session、地图和网络逻辑回归。
- Visual capture、model views、character acceptance 和 world capture：角色、模型及真实 world render。
- `build/glb-inspect ... contract`、`build/rfchar_runtime_test <model.rmesh>`：资产与 runtime 契约。
- `make map-layout`、`tools/map_layout_query.py`：地图布局导出和精确空间查询。
- `tools/gpu_acceptance.ps1`、`tools/gpu_metrics.ps1`：GPU 正确性、生命周期和性能证据。

退出码、标准输出、日志和生成物属于可复核事实。文档与实际行为不一致时，先检查 Makefile、脚本、参数
解析和调用入口，再修正文档。

## 所有权主线

```text
平台事件、网络包
    → process / Core Host / Game runtime 编排
    → single-player、host、client session
    → actor API 与确定性 Game 状态
    → 只读 gameplay projection
    → renderer / HUD / audio / network presentation
```

权威玩法状态归 Game；模式编排归 session；渲染和 HUD 只读玩法或展示状态；客户端预测、校正和插值属于
网络展示链。地图文本、Runtime Map、玩法投影、碰撞和渲染不得互相替代。资产坐标、bind pose、动画求值
和末端渲染补偿也必须保持分层。

## 修改后的文档联动

| 改动 | 同步检查 |
| --- | --- |
| 模块职责、状态所有权或主数据流 | 本页和受影响的架构文档 |
| 构建、脚本、诊断或验收方式 | 相应 guide |
| 地图、资产、动画或协议格式 | 相应 reference、生产者与消费者 |
| 当前执行顺序 | 只更新 `plans/README.md` 和其活动计划 |
| 完成、撤销或被替代的阶段记录 | 移入 `archive/` 并写明当前替代入口 |

新增跨模块功能必须补充本页的任务路由。稳定导航中不维护测试数量、单次性能数字或完整参数表。
