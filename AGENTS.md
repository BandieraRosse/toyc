# Toyc / Rasterfall 仓库协作说明

本仓库源自 Toyc 自托管 C 工具链；当前长期开发主线是 Rasterfall。根目录只维护跨项目规则，各子系统的架构、工作流、当前计划和历史记录统一从 [`docs/README.md`](docs/README.md) 进入。

## 仓库结构与入口

| 范围 | 职责 | 开始任务前先读 |
| --- | --- | --- |
| `rasterfall/`、`gpu/`、`windows/` | Rasterfall 游戏、GPU 后端与 Windows 主开发 lane | `docs/rasterfall/README.md` |
| `compiler/`、`compiler-tests/` | Toyc 编译器、汇编器、链接器与测试 | `docs/toolchain/README.md` |
| `lib/`、`include/` | Tinylibc 和跨平台公共设施 | `docs/toolchain/README.md`；Rasterfall 任务再读其维护入口 |
| `app/`、`tools/` | 示例应用、检查器和离线工具 | `docs/applications/README.md` |
| `llm/` | GPT-2、Qwen2 与共享数值设施 | `docs/llm/README.md` |
| `bootstrap/` | 版本控制内的自举种子 | `docs/toolchain/README.md` |
| `docs/` | 全仓库唯一维护文档根 | `docs/README.md` |

根 `README.md` 是仓库和用户总览；`rasterfall/README.md` 是 Rasterfall 用户入口。维护者文档、当前计划和历史记录不得另建第二套文档根。

## Rasterfall 工作原则

- 阅读 Rasterfall 代码前，必须先从 `docs/rasterfall/README.md` 按任务类型进入对应文档；不要从最大的 `.c` 文件或文件名猜测模块边界。
- `rasterfall/src/rasterfall.c` 只负责进程生命周期、输入、固定步长主循环和顶层编排；session、确定性玩法、渲染、网络与平台状态遵守各自架构文档中的所有权。
- 主机拥有联机权威状态；渲染和 HUD 不修改玩法真值；纯视觉状态不进入 `toy_game`。
- 地图解析、runtime map、玩法投影、碰撞和渲染是不同层；资产坐标、bind pose、动画求值和渲染补偿也必须分层处理。
- Rasterfall 不要求由 Toyc 编译。共享玩法和渲染源码跨平台复用，平台差异留在公共平台层或 `windows/src/`。
- Windows 原生 PowerShell 和物理 GPU 是当前 Rasterfall 开发与签收环境；WSL/Linux 可辅助诊断，不能代替 Windows native present、驱动、窗口生命周期或性能结论。
- 当前工作优先级只以 `docs/rasterfall/plans/README.md` 指向的唯一活动计划为准。

## 构建与验证

Rasterfall 的主要闭环是：

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

修改后先运行离改动最近的验证，再按风险扩大。玩法/session/map 至少构建并运行逻辑回归；渲染、模型和动画使用相应离屏诊断并在可用时实机启动；网络先跑纯逻辑用例，再验证所需拓扑；平台共享代码补对应平台构建。只有修改 Toyc 编译器、公共代码生成路径或影响无法限定时，才扩大到编译器测试。普通 Rasterfall 修改不得运行 `make update-bootstrap`。

Windows GUI 进程、GPU 验收、package 和故障注入的具体等待及日志规则见 `docs/rasterfall/guides/windows-native.md`，不要仅凭 PowerShell 表面返回或单张截图下结论。

## Windows / PowerShell 文本编辑

- 本仓库主要在原生 Windows / PowerShell 下开发；源码、Markdown、JSON、TOML 和其他文本文件统一按 UTF-8 处理，不依赖 Windows PowerShell 5.1 的默认编码。
- 使用 `Get-Content`、`Set-Content`、`Out-File` 等命令处理可能含非 ASCII 字符的文本时，必须显式指定 UTF-8；禁止用默认编码的重定向写入仓库文本。注意 PowerShell 5.1 的 `-Encoding UTF8` 会写入 BOM，写回时必须保留原文件的 BOM 状态，必要时使用显式配置编码的 .NET 或 Python 文件 API。
- 禁止将含中文或其他非 ASCII 字符的 PowerShell here-string 通过管道传给 `python -` 等原生程序；原生进程标准输入编码与文件编码是不同边界，仅在 Python 中指定文件编码不能避免管道中的字符损坏。需要此类脚本时，使用补丁工具创建 UTF-8 脚本文件后执行。
- 优先使用 `apply_patch` 做小范围、可核对的编辑；脚本化修改优先使用结构化定位或短 ASCII 锚点，禁止通过 PowerShell 对长中文或非 ASCII 段落做整段精确替换。
- Python 读写文本必须显式指定 `encoding="utf-8"`，已有 BOM 时显式使用 `utf-8-sig`。写入前检查并保留原文件的换行符（CRLF/LF）和 BOM，避免文本 API 的默认换行转换；未经任务要求，不做全文件重写或编码、换行规范化。
- 精确匹配或补丁失败后，先以明确的 UTF-8 编码重新读取磁盘上的相关区域，核对当前内容和编码后再修改；不得猜测原文或反复重试过期文本。发现乱码或问号替换时立即停止写入，先修正传输或解码方式。
- 修改文本后必须查看 `git diff` 并运行 `git diff --check`，确认中文可读、改动范围正确，没有意外 BOM、换行变化或无关重写；文档改动另运行适用的文档检查。

## 修改与文档

- 保留用户已有工作区修改，不格式化或改写无关文件。
- 新增编译单元时检查根 Makefile、`windows/Makefile` 和适用的 self 规则；新增资源时检查加载、内嵌依赖和 Windows package 复制规则。
- 不提交 `build/`、`tmp/`、依赖缓存或私有资源。
- 重大重构、所有权变化、数据流变化、重要文件迁移或重大功能变动，必须在同一改动中更新对应架构文档和 `docs/rasterfall/README.md` 的任务路由。
- 稳定契约、操作指南、当前计划和历史现场必须分开；归档不能作为当前设计依据。完整规则见 `docs/repository/documentation.md`。
- 易变参数以程序 `--help` 和脚本实际输出为准。文档与代码不一致时，核对构建入口和实际行为，并在同一改动中修正文档。
- 提交信息优先使用简洁中文标题；多段正文使用多个 `git commit -m` 参数或真正换行。

