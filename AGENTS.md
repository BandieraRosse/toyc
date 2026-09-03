# Rasterfall 项目协作说明

本仓库源自 Toyc：`compiler/` 是面向 Linux x86_64 的自托管 C 工具链，`lib/` 和 `include/`
包含 Tinylibc 与公共平台设施。当前开发重点是 `rasterfall/`。编译器时期的完整代理说明保存在
`docs/AGENTS-toyc-history.md`；用户文档和语言特性仍分别以 `README.md`、`README_en.md` 和
`toyc-c-features.md` 为准。

## 开始 Rasterfall 任务前

**阅读代码之前，必须先打开 `rasterfall/docs/README.md`，根据任务类型进入对应模块文档，
再按文档给出的状态所有者和入口查源码。** 不要从最大的 `.c` 文件盲目搜索，也不要仅凭文件名
推断模块边界。

文档索引：

- `rasterfall/docs/README.md`：总入口、任务到文件映射、架构主线。
- `rasterfall/docs/runtime.md`：启动、参数、输入、主循环和音画同步。
- `rasterfall/docs/gameplay.md`：玩法核心、session、地图和 AI。
- `rasterfall/docs/rendering.md`：世界渲染、角色、HUD、特效和性能。
- `rasterfall/docs/assets-animation.md`：资源、模型、蒙皮、IK、VMD/GLB 和转换工具。
- `rasterfall/docs/asset-pipeline.md`：资产转换、LOD、检查器和离屏诊断。
- `rasterfall/docs/map-format.md`：地图文本格式和跨层修改要求。
- `rasterfall/docs/networking.md`：协议、快照、预测、可靠事件和房间发现。
- `rasterfall/docs/build-platforms.md`：Linux/Windows 构建、平台边界和验证矩阵。
- `rasterfall/docs/animation-architecture.md`、`rasterfall/docs/network-architecture.md`：专题设计。
- `rasterfall/docs/asset-sources.md`：资源来源、许可状态和发布边界。
- `rasterfall/docs/archive/`：历史现场记录，不作为当前设计依据。

文档与代码不一致时，以 Makefile、脚本和实际行为为准，同时修正文档。每篇导航文档顶部记录
“文档更新”和“源码核对基线”；更新内容时一并刷新这两个字段。

**完成重大重构、模块职责调整、数据流变化、重要文件迁移或重大功能变动后，必须在同一改动中
更新 `rasterfall/docs/` 的总索引与受影响模块文档。** 新增跨模块功能时，应补充“任务到文件”
定位和跨层联动点，不能只更新面向玩家的 README。

## 关键架构原则

- `rasterfall/src/rasterfall.c` 只做进程生命周期、输入、固定步长主循环及顶层音画网络编排。
- `rasterfall/src/rasterfall_session.c` 负责编排单机、主机和客户端会话。
- `rasterfall/lib/game.c` 与 `rasterfall/include/toy_game.h` 拥有确定性规则和权威玩法状态。
- 渲染和 HUD 读取玩法/展示状态，不应修改权威结果；纯视觉状态不要塞进 `toy_game`。
- 联机主机权威；客户端预测、校正和插值属于网络展示链路。协议显式编码，不发送原始 C 结构。
- Rasterfall 不承诺旧版本兼容；联机双方始终假设运行同一份最新代码，地图与资产也始终假设为同一最新版本，不为跨版本联机或跨版本地图加载保留兼容路径。
- 地图功能需区分文本解析、玩法绑定、碰撞/交互和渲染，不能用可见几何代替玩法碰撞。
- 资产坐标、bind pose、动画求值和渲染补偿分层处理，不用末端视觉偏移掩盖上游资产错误。
- Rasterfall 不要求由 Toyc 编译。Linux 版本以 GCC 验证，不为 Toyc 兼容限制 Rasterfall 实现。
- Linux 和 Windows 共用玩法与渲染源码；平台差异优先留在公共平台层或 `windows/src/`。
- 保留 freestanding Linux 路径，不无意引入宿主 libc 依赖。

## 重要目录

- `rasterfall/include/`：模块公开状态和接口，定位所有权时优先查看。
- `rasterfall/lib/`：可脱离窗口验证的玩法、地图解析和声音合成核心。
- `rasterfall/src/`：运行编排、session、网络、渲染、界面和模型运行时。
- `rasterfall/assets/`：公开资源；`rasterfall/private-assets/` 是可选本地资源。
- `lib/`、`include/`：Tinylibc 及共用平台、窗口、渲染、输入、音频和资源设施。
- `windows/`：MinGW-w64 + SDL2 平台适配和打包。
- `app/`、`tools/`：模型检查、格式转换、导入和 LOD 工具。
- `build/`、`tmp/`：本地生成物，不提交。

## 构建与验证

```sh
make generate-assets
make app-rasterfall
build/rasterfall
build/rasterfall --logic-test

make win-deps
make win-rasterfall
make win-rasterfall-package
```

Linux 默认从仓库目录读取资产；内嵌公开资源使用 `make rasterfall-embedded`。可用
`make RASTERFALL_OPT=-O0 build/rasterfall` 做优化级别对照。

修改后先运行最近的验证，再按风险扩大：玩法/session/map 至少构建并运行 `--logic-test`；渲染、
模型和动画使用相关 dump、benchmark 或诊断参数并在可用时实际启动；网络先跑纯逻辑用例，再按
`rasterfall/docs/network-architecture.md` 验证所需拓扑；共享平台或 Windows 改动补对应平台构建。
无图形、音频、网络或交叉编译环境时，明确报告未覆盖项。

只有改动 Toyc 编译器、公共代码生成路径或影响无法限定时，才按
`docs/AGENTS-toyc-history.md` 扩大到编译器测试。不要在普通 Rasterfall 修改中运行
`make update-bootstrap`。

## 修改与提交约束

- 保留用户已有工作区修改，不格式化或改写无关文件。
- 新增 Rasterfall 编译单元时同时检查根 Makefile、`windows/Makefile` 和适用的 self 规则。
- 新增资源时同时检查文件加载、内嵌资源依赖和 Windows package 复制规则。
- 不提交 `build/`、`tmp/`、`/tmp`、依赖缓存或私有资源。
- 测试数量和阶段性结果不要写入稳定导航文档；结果以实际输出为准。
- 提交信息优先使用简洁中文标题。多段正文使用多个 `git commit -m` 参数或真正换行，不在
  `-m` 字符串中写字面量 `\n`。
