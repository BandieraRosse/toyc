# Rasterfall 代码导航

> 最后更新：2026-09-03
> 依据提交：`2698c813cbd04ed2c197dc84101ae02f0b9b02a4`（增加 Rasterfall 代码导航文档）

本目录面向接手 Rasterfall 任务的编码代理。目标不是介绍玩法，而是先把问题归到正确的
状态所有者和文件，再开始搜索。命令、资源导入方法和用户可见特性仍以
[`../README.md`](../README.md) 为准。

## 先读哪一篇

| 任务或症状 | 首先阅读 | 主要入口 |
| --- | --- | --- |
| 启动、参数、输入、暂停、主循环、音画同步 | [runtime.md](runtime.md) | `src/rasterfall.c` |
| 武器、敌人、碰撞、寻路、波次、商店、AI | [gameplay.md](gameplay.md) | `lib/game.c`、`src/rasterfall_session.c` |
| 地图格式、关卡实体、拾取物、出生点 | [gameplay.md](gameplay.md) | `lib/map.c`、`src/rasterfall_map.c` |
| 场景、角色、HUD、特效、第一人称武器、性能 | [rendering.md](rendering.md) | `src/rasterfall_render.c` |
| 模型、蒙皮、IK、VMD/GLB、动作重定向 | [assets-animation.md](assets-animation.md) | `src/rasterfall_model.c` |
| 联机协议、快照、预测、可靠事件、房间发现 | [networking.md](networking.md) | `src/rasterfall_net.c` |
| Linux/Windows 平台差异、构建、测试 | [build-platforms.md](build-platforms.md) | `Makefile`、`windows/Makefile` |

## 架构主线

```text
平台事件/网络包
      ↓
src/rasterfall.c                 进程、菜单、输入、固定步长帧循环
      ↓
src/rasterfall_session.c         单机/主机/客户端会话编排
      ↓
lib/game.c                       确定性玩法状态和规则
      ↓
src/rasterfall_render.c + HUD    只读玩法状态并生成画面
      ↓
toy_renderer / window / audio    仓库公共平台层
```

网络主机运行权威会话；客户端通过 `rasterfall_net.c` 的快照、预测与校正形成展示状态。
不要把纯视觉状态塞进 `toy_game`，也不要让渲染器修改权威玩法结果。

## 目录边界

- `rasterfall/include/`：模块公开结构、枚举和函数契约；定位状态所有权时先看对应头文件。
- `rasterfall/lib/`：可脱离窗口和渲染验证的玩法、地图解析和声音合成核心。
- `rasterfall/src/`：应用编排、网络、渲染、资产运行时和界面。
- `rasterfall/assets/`：公开运行时地图、纹理、音效和模型。
- `rasterfall/private-assets/`：可选本地资产；代码不能假设每个工作区都有它。
- `app/`、`lib/`、`include/`、`windows/`、`tools/` 中也有 Rasterfall 使用的转换器与平台层，
  详见各模块文档。

已有专题文档继续承担深入说明：

- [`../ANIMATION_ARCHITECTURE.md`](../ANIMATION_ARCHITECTURE.md)：动画数据流和扩展边界。
- [`../NETWORK_ARCHITECTURE.md`](../NETWORK_ARCHITECTURE.md)：联机状态分类和房间生命周期。
- [`../PROJECT_HANDOFF.md`](../PROJECT_HANDOFF.md)：模型/动画实验现状和本地资源背景，不作为通用架构入口。

## 修改时的定位原则

先找“谁拥有状态”，再找“谁展示状态”。改公共结构时同时搜索其序列化、测试和所有调用者。
尤其注意以下跨层联动：

- 修改 `toy_game.h` 的武器、敌人、事件或结构布局：检查 `lib/game.c`、session、HUD、渲染和网络编码。
- 修改地图语义：检查 `toy_map.h`/`lib/map.c` 的解析、`rasterfall_map.c` 的绑定、玩法碰撞和渲染。
- 修改角色动画：检查角色选择、会话动画状态、模型求值、渲染以及网络动画字段。
- 修改命令行或诊断模式：从 `rasterfall_options.c` 到 `rasterfall.c` 的早退分支一起核对。
