# Rasterfall

> 文档更新：2026-09-09
> 源码核对基线：工作区（用户入口保持稳定；易变参数以 `build/rasterfall --help` 为准）

Rasterfall 是 Toyc 仓库中的 freestanding 第一人称合作射击实验，使用软件光栅器，包含地图、
战斗、波次、AI 队友、音频、联机和静态/骨骼模型。Linux 版本使用仓库内 Tinylibc 与 Wayland/音频
后端；Windows 版本使用 MinGW-w64 和 SDL2。

本页是用户入口，只保留构建、启动和稳定工具入口。维护者应从
[`docs/README.md`](docs/README.md) 开始；资产、模型、地图、网络和平台细节分别见对应专题文档。

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

## 常用运行与验证

```sh
build/rasterfall --logic-test
build/rasterfall --help
build/rasterfall --host --port 28460
build/rasterfall --connect 127.0.0.1 --port 28460
make app-glb-inspect app-vmd-inspect
build/glb-inspect --self-test
build/rfchar_runtime_test <model.rmesh>
```

角色和模型观察使用 `--model-views`、`--model-pose-views`、`--character-acceptance` 和
`--character-world-capture`；具体流程见 [`docs/character-assets.md`](docs/character-assets.md)
和 [`docs/rendering.md`](docs/rendering.md)。所有易变命令行选项以 `build/rasterfall --help` 的
当前输出为准。逻辑测试不能代替视觉检查或真实多人联机验收。

## 开发文档

- [`docs/README.md`](docs/README.md)：代码导航总入口。
- [`docs/animation-architecture.md`](docs/animation-architecture.md)：模型与动画架构契约。
- [`docs/network-architecture.md`](docs/network-architecture.md)：联机架构与扩展边界。
- [`docs/asset-sources.md`](docs/asset-sources.md)：资源来源、许可和发布检查。
- [`docs/archive/project-handoff-2026-09.md`](docs/archive/project-handoff-2026-09.md)：历史现场记录，
  不代表当前实现。
