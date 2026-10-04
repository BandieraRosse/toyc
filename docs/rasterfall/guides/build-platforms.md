# 构建、平台与验证

> 状态：当前
> 所有者：Rasterfall 构建与平台
> 最近核对：2026-09-26

Rasterfall 的主开发和验收环境是 Windows 原生 PowerShell、MSYS2/MinGW 与物理 GPU。入口为 `windows/NativeCodex.ps1`；具体依赖、等待和日志规则见[Windows Native](windows-native.md)。Linux/WSL 可以辅助编译或诊断，但不代替 Windows native present、驱动、窗口生命周期和性能结论。

## Windows 主闭环

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

`build` 生成 Windows 玩家程序，并在已有运行目录时同步其中的 exe；`run`、`test`、`gpu-test` 和 `acceptance` 更新运行目录的 exe 与资源，不创建 zip。仅显式执行 `package` 才生成 `build-windows/rasterfall-windows.zip`。`test` 运行逻辑回归；`gpu-test` 在运行目录中执行 120 帧硬件 Scene；`acceptance` 增加 normal-frame 与视觉 capture。`run` 可把额外参数传给运行目录中的 `rasterfall.exe`。命令可用性和具体参数以脚本 `help` 与程序 `--help` 为准。

## GPU service 与测试

`gpu/src/rf_gpu_vulkan_backend.c` 提供 Vulkan device、通用 compute framebuffer smoke、Scene graphics/skinning 与 native present。`gpu/src/rf_gpu_graphics_test.c` 覆盖 Scene layers、颜色合批和资源复用；GPU service/probe 测试检查设备能力与初始化边界。CPU 软件 renderer 保持独立构建入口。

旧 GPU Compute Raster pack/bin、SPIR-V、mixed executor 和 bridge 的构建目标已移除。历史构建与诊断说明见[退役归档](../archive/gpu-compute-retirement/README.md)，不能用于当前验收。

修改源文件列表时检查根 `Makefile`、`windows/Makefile`、self 构建规则与 package 资源复制。新增资产同时检查加载和内嵌依赖。

## 按模块导出源码集合

在仓库根目录用 Git Bash 或 MSYS2 Bash 运行 `bash scripts/merge-rasterfall.sh`。
脚本针对当前仓库布局读取工作区内容（包含未提交修改），输出到 `tmp/`，不要求先构建。

先读 `rasterfall-project.txt`：其中记录版本、每份集合的体量、文件归属、跨模块任务的组合建议，
以及资源目录树。七份源码集合按目录与模块职责共同划分，每个收录文件只出现一次：

| 输出文件 | 范围 |
| --- | --- |
| `rasterfall-source-runtime.txt` | 进程与 Core Host、运行调度、输入、启动界面、玩家 UI、命令、剧情、音频与实验控制 |
| `rasterfall-source-gameplay.txt` | Game 规则、战斗、导航、制造、session、RTS 指挥、联机与协议 |
| `rasterfall-source-maps.txt` | 地图解析、Runtime Map、玩法地图投影、World Content 与布局工具 |
| `rasterfall-source-rendering.txt` | 世界渲染、GPU Scene、天空、光照、特效与机器表现 |
| `rasterfall-source-characters.txt` | 模型、角色与敌人表现、动画、姿态、角色 Scene 来源与 viewmodel |
| `rasterfall-source-platform.txt` | GPU 后端及着色器、Windows、公共库与头文件、构建及 GPU 生成工具 |
| `rasterfall-source-diagnostics.txt` | 逻辑与 GPU 测试、截图、诊断脚本及收录的实验工具 |

集合保留完整文件，不按行数截断；跨集合接口通过总索引定位，不重复复制头文件。
源码文件先逐个复制到临时目录，再从同一副本统计和合并，避免并行编辑造成索引与内容不一致；
不同文件的采集时间仍有先后，合集不代表整个工作区在同一时刻的冻结版本。
目录扫描包含 `rasterfall/lib/` 和 `.glsl`。工具只收录脚本列明的诊断、布局等工具，
不是整个资产生产工具链的完整备份。生成的 `rf_gpu_graphics_spirv.inc` 只列入索引，
以着色器源码和生成器作为阅读入口；其他影响语义的生成头文件仍随模块导出。

`rasterfall-docs.txt` 继续单独收录当前维护文档及协作入口，排除 `archive/`。
资源只列目录索引，不复制资源内容。旧的 `rasterfall-source.txt` 和
`rasterfall-source-and-headers.txt` 会在成功生成后清理，避免误用旧全集。
