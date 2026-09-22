# Rasterfall 项目协作说明

本仓库源自 Toyc：`compiler/` 是面向 Linux x86_64 的自托管 C 工具链，`lib/` 和 `include/`
包含 Tinylibc 与公共平台设施。当前开发重点是 `rasterfall/`，并处于 GPU 渲染持续开发阶段。
Rasterfall 的主要开发、构建编排、实机验证和签收环境已经转为 Windows 原生 PowerShell；WSL
仅保留为辅助/历史兼容路径，不保证随主线同步更新、可构建或运行结果正确。编译器时期的完整代理说明保存在
`docs/AGENTS-toyc-history.md`；用户文档和语言特性仍分别以 `README.md`、`README_en.md` 和
`toyc-c-features.md` 为准。

## 了解 Toyc 历史时

需要了解 SC7、Tinylibc、ToyCCompiler、Toyc 的项目传承、历史时间线或开发方式时，先阅读
`docs/README.md` 和 `docs/archaeology/README.md`。继续考证具体时期前，再从
`docs/archaeology/questions.md` 选择问题，并按其中链接进入对应时期文档与证据索引；不要只凭
当前目录结构、单条提交说明或事后 README 推断历史结论。

## 开始 Rasterfall 任务前

**阅读代码之前，必须先打开 `rasterfall/docs/README.md`，根据任务类型进入对应模块文档，
再按文档给出的状态所有者和入口查源码。** 不要从最大的 `.c` 文件盲目搜索，也不要仅凭文件名
推断模块边界。

## 文档层级与 Agent CLI 事实入口

Rasterfall 的文档分三层维护：根目录 `README.md` 是仓库总览；`rasterfall/README.md` 是
Rasterfall 面向用户的稳定入口；`rasterfall/docs/README.md` 及其模块文档是维护者导航、状态
所有权和工具链细节。不要把 `rasterfall/README.md` 当作历史文件，也不要在它复制易变的完整
参数表；历史现场只放在 `rasterfall/docs/archive/`。

Agent 获取 Rasterfall 重要事实时，优先使用下面这些可执行 CLI 的实际输出，再用文档解释上下文：

- `build/rasterfall --help`：当前运行参数的唯一完整清单；参数新增、删除或兼容别名变化后，先核对它。
- `build/rasterfall --logic-test`：玩法、session、地图和网络逻辑的无窗口回归入口。
- `build/rasterfall --visual-capture ...`、`--model-views`、`--model-pose-views`、
  `--character-acceptance`、`--character-world-capture`：角色、模型和真实 world render 的离屏观察入口。
- 观察模型时优先使用组图脚本提高审阅效率：`python3 tools/character_lab_sheet.py` 生成当前
  Humanoid V2 的姿态/四视角合集，`python3 tools/character_world_sheet.py` 生成 near/mid/far
  实景距离与 old/idle/aim/motion 合集；脚本调用上述 CLI 后再拼接 PNG，原始 BMP 保留在输出目录。
- `build/glb-inspect ... contract`、`build/rfchar_runtime_test <model.rmesh>`：RFCHAR 输入契约和
  RFM2/RFCHAR runtime 加载门禁。
- `build/vmd_inspect ...`：仅用于旧 PMX/VMD 兼容诊断，不作为新 RFCHAR 资产主路径。
- `make map-layout`、`tools/map_layout_query.py`：地图布局 PNG/JSON 导出和精确空间查询。

上述 CLI 的退出码、标准输出和生成物属于可复核事实；文档与实际输出不一致时，先按“文档与代码
不一致”规则核对 Makefile、参数解析和调用入口，再修正文档。不要仅凭截图或历史归档推断当前状态。

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
- `rasterfall/docs/gpu-current-state.md`：GPU 当前实现、实机验证和性能快照入口。
- `rasterfall/docs/gpu-raster-bridge-plan.md`：当前 GPU 硬件开发的唯一优先计划；顶部记录当前串行执行链
  以及 RB 问题域与 M 实施切片的关系。旧阶段计划仅保存在 `rasterfall/docs/archive/`。
- `rasterfall/docs/windows-native-codex.md`：当前 Rasterfall 主开发 lane；PowerShell 构建、package、
  GPU 实机运行和验收入口。
- `rasterfall/docs/animation-architecture.md`、`rasterfall/docs/network-architecture.md`：专题设计。
- `rasterfall/docs/asset-sources.md`：资源来源、许可状态和发布边界。
- `rasterfall/docs/archive/`：历史现场记录，不作为当前设计依据。

文档与代码不一致时，以 Makefile、脚本和实际行为为准，同时修正文档。每篇导航文档顶部记录
“文档更新”和“源码核对基线”；更新内容时一并刷新这两个字段。

**完成重大重构、模块职责调整、数据流变化、重要文件迁移或重大功能变动后，必须在同一改动中
更新 `rasterfall/docs/` 的总索引与受影响模块文档。** 新增跨模块功能时，应补充“任务到文件”
定位和跨层联动点，不能只更新面向玩家的 README。

## 关键架构原则

- 代码首先为维护者和大模型协作阅读而写：使用能表达领域语义的稳定命名、短而单一职责的函数、显式的
  数据流与所有权边界，优先沿用仓库现有模式。避免依赖隐含调用顺序、跨文件隐藏状态、语义不明的缩写、
  过度宏技巧和无必要的紧凑写法；复杂约束应在接口附近说明“为什么”，并让测试与诊断入口能够直接定位
  关键状态。不要为了形式上的拆分制造大量只有一次调用、不能独立表达语义的薄包装。
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
- 当前 Rasterfall 开发决策以 Windows 原生 PowerShell lane 和物理 GPU 证据为准。WSL、llvmpipe
  或 Linux hosted Vulkan 可以用于辅助编译和 correctness 诊断，但不能代替 Windows native present、
  驱动、窗口生命周期与性能验收；WSL 路径不承诺持续维护或正确性。
- 当前 GPU 性能开发优先遵循 `rasterfall/docs/gpu-raster-bridge-plan.md`；具体正在执行的切片以该文档顶部
  “当前唯一执行链”为准。不因单次帧数据直接扩大 Graphics 类型；迁移优先选择数据证明高成本且能减少
  真实 Draw/Raster run 的 opaque 内容，不把透明、粒子、复杂 VFX、新材质体系或低收益 Draw 微优化顺带混入。

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

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

这是当前 Rasterfall 的主要开发闭环。Linux/freestanding 构建仍可使用 `make rasterfall`、
`build/rasterfall --logic-test`；WSL 可继续尝试这些入口，但属于 best-effort 辅助路径，不作为
当前 GPU 开发的签收依据，也不保证其依赖、窗口、音频或 Vulkan 路径保持可用。

修改后先运行最近的验证，再按风险扩大：玩法/session/map 至少构建并运行 `--logic-test`；渲染、
模型和动画使用相关 dump、benchmark 或诊断参数并在可用时实际启动；网络先跑纯逻辑用例，再按
`rasterfall/docs/network-architecture.md` 验证所需拓扑；共享平台或 Windows 改动补对应平台构建。
无图形、音频、网络或交叉编译环境时，明确报告未覆盖项。

只有改动 Toyc 编译器、公共代码生成路径或影响无法限定时，才按
`docs/AGENTS-toyc-history.md` 扩大到编译器测试。不要在普通 Rasterfall 修改中运行
`make update-bootstrap`。

## PowerShell 与 Windows 进程注意事项

Rasterfall 当前主要在 Windows 原生 PowerShell 下开发。构建、运行、GPU 测试和签收优先使用仓库已有的
`windows/NativeCodex.ps1` 和明确的 package 工作目录；不要先在 WSL 复现再把 WSL 结果当作 Windows
GPU 结论。PowerShell/Win32 进程行为有以下已确认陷阱；后续 Agent 遇到新的可复现问题时，应在本节继续补充：

- `rasterfall.exe` 使用 GUI subsystem。PowerShell 的 `& .\rasterfall.exe ...`、`$LASTEXITCODE`，以及
  `cmd /c` 在不同的输出继承或重定向方式下可能提前返回，不能据此证明子进程已退出。执行长时或故障
  注入门禁时，必须同时核对目标进程、最终日志和预期帧数；不要让多个 GPU 验证实例并发运行。
- `Start-Process -Wait -PassThru` 通常适合取得真实退出码，但当前环境若同时存在大小写不同的 `Path`
  与 `PATH` 环境项，可能抛出“字典中已添加相同键”的异常。遇到此问题不要反复重试或并发启动；改用
  能明确等待的单进程包装方式，并在启动后用 `Get-Process`/日志确认生命周期。
- PowerShell 中 `&` 是调用/控制运算符。需要把 `&` 交给 `cmd.exe /c` 时，应将完整命令作为一个字符串
  参数传递；不要让外层 PowerShell 先解析它。复杂重定向尤其要先用一个短用例验证实际等待与退出码。
- 删除或覆盖 `rasterfall.log` 前，先确认没有仍在写该文件的 `rasterfall` 进程。出现“文件正在由另一
  进程使用”通常说明上一个 GUI 进程仍存活，而不是测试已经完成。
- `Get-CimInstance`/`Get-PnpDevice` 的 GPU 枚举可能因权限失败；这不等于 Vulkan 不可用。GPU 事实以
  程序自身的 adapter 输出、退出码和帧审计为准，并单独报告系统枚举未覆盖。
- 卡死进程先精确查询 PID、路径和启动时间，再只终止目标 `rasterfall.exe`；终止后再次查询，避免遗留
  进程污染后续日志。不要用宽泛的递归或名称模式清理无关进程。

## 修改与提交约束

- 保留用户已有工作区修改，不格式化或改写无关文件。
- 新增 Rasterfall 编译单元时同时检查根 Makefile、`windows/Makefile` 和适用的 self 规则。
- 新增资源时同时检查文件加载、内嵌资源依赖和 Windows package 复制规则。
- 不提交 `build/`、`tmp/`、`/tmp`、依赖缓存或私有资源。
- 测试数量和阶段性结果不要写入稳定导航文档；结果以实际输出为准。
- 提交信息优先使用简洁中文标题。多段正文使用多个 `git commit -m` 参数或真正换行，不在
  `-m` 字符串中写字面量 `\n`。
