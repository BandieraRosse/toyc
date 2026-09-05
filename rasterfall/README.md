# Rasterfall

> 文档更新：2026-09-03
> 源码核对基线：`75a10cd`（将项目协作说明转向 Rasterfall）

Rasterfall 是 Toyc 仓库中的 freestanding 第一人称合作射击游戏实验，使用软件光栅器，包含
地图、战斗、波次、AI 队友、音频、局域网/公网联机以及静态和骨骼模型。Linux 版本使用仓库内
Tinylibc 与 Wayland/音频后端；Windows 版本使用 MinGW-w64 和 SDL2。

面向代码维护者和 Codex 的模块导航从 [`docs/README.md`](docs/README.md) 开始。资源来源、
许可状态和发布限制见 [`docs/asset-sources.md`](docs/asset-sources.md)。

## 构建与运行

在仓库根目录构建 Linux 版本：

```sh
make generate-assets
make app-rasterfall
build/rasterfall
```

Linux 默认从 `rasterfall/assets/` 读取资源。需要把公开资源嵌入程序时使用：

```sh
make rasterfall-embedded
build/rasterfall-embedded
```

Windows 版本使用独立工具链，不要求 Toyc 输出 PE/COFF：

```sh
make win-deps
make win-rasterfall
make win-rasterfall-package
```

生成物分别为 `build/rasterfall.exe` 和 `build/rasterfall-windows.zip`。Windows 程序以 EXE
所在目录为资源根目录，构建及打包细节见 [`../windows/README.md`](../windows/README.md)。

## 操作

- `WASD`：移动；鼠标或方向键：观察。
- 鼠标左键或 Enter：射击；空格：跳跃；`R`：换弹；`1`/`2`：切换武器。
- `E`：交互；`Esc`：暂停或恢复。
- 波次间可使用商店、拾取武器和弹药，并管理 AI 队友。

游戏启动菜单提供单机、局域网房间和公网房间入口。公网房间 `0000`～`4999` 使用 UDP 打洞，
`5000`～`9999` 使用 relay；失败时不会在两种模式之间静默回退。联机是可信玩家之间的合作模式，
不以抵抗恶意客户端为设计目标。完整约束见
[`docs/network-architecture.md`](docs/network-architecture.md)。

## 资源目录

- `assets/maps/`：文本地图。
- `assets/models/`：公开 RFM2/RMESH 运行时模型。
- `assets/textures/`：TTEX 纹理。
- `assets/audio/`：TSND 音效。
- `private-assets/`：可选的本地受限资源，不属于公开发布内容。

地图的可见几何与碰撞属性相互独立；格式见 [`docs/map-format.md`](docs/map-format.md)，玩法绑定
入口见 [`docs/gameplay.md`](docs/gameplay.md)。模型运行时见
[`docs/assets-animation.md`](docs/assets-animation.md)，PMX/GLB/VMD 工具与诊断命令见
[`docs/asset-pipeline.md`](docs/asset-pipeline.md)。

## 常用验证

```sh
build/rasterfall --logic-test
make app-vmd-inspect app-glb-inspect
build/glb-inspect --self-test
build/rasterfall --actor-performance 30 5 8
```

逻辑测试不能代替模型动画的视觉检查或真实多人联机验收。按修改类型选择完整验证方式，参见
[`docs/build-platforms.md`](docs/build-platforms.md)。所有命令行诊断选项以
`build/rasterfall --help` 的当前输出为准。

## 开发文档

- [`docs/README.md`](docs/README.md)：代码导航总入口。
- [`docs/animation-architecture.md`](docs/animation-architecture.md)：模型与动画架构契约。
- [`docs/network-architecture.md`](docs/network-architecture.md)：联机架构与扩展边界。
- [`docs/asset-sources.md`](docs/asset-sources.md)：资源来源、许可和发布检查。
- [`docs/archive/project-handoff-2026-09.md`](docs/archive/project-handoff-2026-09.md)：历史现场记录，
  不代表当前实现。
