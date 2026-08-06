# Toyc

[English](README_en.md)

Toyc 是一个面向 Linux x86_64 的小型、自托管 C 工具链，源自
[Tinylibc](https://github.com/WHU-SC7/Tinylibc) 和 ToyCCompiler。工具链自身不链接
libc，运行时直接使用 Linux 系统调用；仓库同时包含 Tinylibc、示例程序和一个小型
GPT-2 实现。

## 工具链

| 程序 | 用途 |
|---|---|
| `toyc` | C 源码 → ELF64 目标文件 |
| `toyas` | x86_64 汇编 → ELF64 目标文件 |
| `toyld` | 目标文件/归档 → 静态可执行文件 |
| `toyar` | ar 归档创建、查看与提取 |
| `toypp` | 独立预处理器（可选目标） |

## 构建

需要 Linux x86_64、GNU make、GCC 和 GNU binutils（`as`、`ld`、`ar`）。默认构建
由 GCC/binutils 生成工具链；`self-*` 目标才使用生成的 Toyc。

```sh
make                    # build/{toyc,toyas,toyld,toyar}
make build/toypp        # 可选：构建独立预处理器
make self-lib           # 用 build/toyc 构建 Tinylibc
make self-app           # 用 build/toyc 构建全部应用
make clean              # 删除 build/ 和 tmp/
```

也可使用 `make lib`、`make app` 或 `make app-<name>` 由 GCC 构建库、全部应用或单个
应用；`make self-app-<name>` 是对应的自托管构建。

### 资产工厂 v0.1

`make validate-assets` 用 GCC 构建离线工具 `build/toyasset`，校验仓库内提交的
小型 `.ttex`、`.tsnd` 和 `.tmesh` 测试资产。原始 PNG/JPEG/WAV/OBJ 需要离线用
`build/toyasset convert` 转换为运行时专用格式，来源文件和大型中间产物不入库
（见 `.gitignore`）。wayland_fps 的 8 种核心音效（`assets/generated/sfx_*.tsnd`）
由 `make generate-assets` 链接 `lib/game/sfx.c` 引擎离线渲染，输出确定性可复现，
无需外部来源文件；wayland_fps 启动时加载播放，缺失时回退程序合成。游戏运行时
只读取带 magic、版本和显式小端字段的格式，不解析
PNG/JPEG/WAV/OBJ 容器。格式说明见 [assets/README.md](assets/README.md)。
v0.1 不包含压缩、FBX、glTF、骨骼动画或 GUI 编辑器。

FPS 的 v0.2 纹理切片默认加载 `assets/generated/wall.ttex`，将 nearest RGB888
采样用于墙、地板和箱体；`--no-textures` 切回纯色，`--texture-stats` 输出纹理
三角形、像素和占位纹理统计。每 5 秒向终端输出一次性能统计：平均帧率、帧渲染
时间均值/p95/p99/最长、帧间隔（wall/wait/stall，用于解释平均帧率与活跃渲染
耗时的缺口），以及 logic/begin/scene/enemies/raster/overlay/present 各阶段
平均每帧的三角形、顶点、像素与耗时（及占比），外加光栅化像素漏斗（包围盒
扫描→覆盖测试→深度通过）与纯色/纹理路径拆分；退出时再输出全量汇总；
`--no-stats` 关闭该输出。可用 `build/wayland_fps --frames 300 --texture-stats`
在 Wayland 环境预览，`build/wayland_fps --logic-test` 为无窗口回归预览。

`bootstrap/` 保存版本控制内的种子二进制。它们用于阶段性的自举检查，不参与默认
`make`，而且可能落后于源码：

```sh
make update-bootstrap       # 有意更新种子；会修改跟踪的二进制
./bootstrap-selfhost.sh     # 种子 → stage 2，并运行自包含测试
./bootstrap-to-10.sh        # 验证 stage 2…10 字节级收敛
```

## 测试

```sh
make test               # 58 个常规编译/运行测试
make test-selfhost      # 42 个无 toyc_rt 的自包含测试
make test-source        # 8 个编译器源码级测试
make test-lib           # 34 个库编译单元 + 16 个功能套件
make test-error         # 16 个诊断测试
make test-toyld         # 42 个使用 toyld 的链接/运行测试
make test-toyar         # 5 个归档器测试
make test-toyld-archive # 2 个归档链接测试
make test-toyld-self    # toyld 两阶段字节一致性
make test-llm           # 29 个 GPT-2 数值/前向传播测试
make test-llm-qwen2     # Qwen2 算子、checkpoint 和单 token 前向测试
make test-all           # 核心聚合目标；不包含 test-toyld 和 test-llm
```

Qwen2.5 推理器可直接读取 Hugging Face 的单文件 `model.safetensors`，支持 BF16
和 F32 权重。旧的 FP32 导出目标仍保留用于格式兼容和调试：

```sh
make export-qwen2 \
  QWEN2_MODEL_DIR=/path/to/Qwen2.5-0.5B-Instruct \
  QWEN2_CHECKPOINT=llm/models/qwen2.5-0.5b-instruct/model-f32.bin
```

只需下载标准 Hugging Face 模型文件，不需要安装 Python 包：

```sh
make download-qwen2
make llm-qwen2
./build/llm-qwen2 \
  --model llm/models/qwen2.5-0.5b-instruct \
  --prompt "你好，请介绍一下自己。" --steps 32 --context 2048
```

C 端直接解析 `config.json`、`tokenizer.json` 和单文件 `model.safetensors`，执行
byte-level BPE prompt 编码、UTF-8 token 解码、采样和 KV-cache 推理。
使用 ModelScope 的 Qwen2.5-0.5B-Instruct 实测时，C 与 PyTorch 参考实现的
最后位置 logits argmax 和 top-10 完全一致，最大绝对误差约为 `5.01e-5`；greedy
生成可正常输出中文并连续推进 KV cache。

`make test-self-app` 只测试已经存在的 `build/*_self`，因此应先运行
`make self-app`；否则缺少的程序会被跳过。

### 2026-07-31 实测

在受限容器中，本次运行结果为：

- `make test`：58/58；`make test-source`：8/8；`make test-error`：16/16。
- `make test-toyar`：5/5；`make test-toyld-archive`：2/2；
  `make test-toyld-self`：两阶段字节一致。
- `make test-llm`：29/29。
- `make test-selfhost` 和 `make test-toyld`：均为 40/42。两个用例调用根目录路径上的
  `renameat2`，容器返回 `EROFS`，而测试固定期待 `ENOENT`；这是环境相关的断言差异。
- `make test-lib`：编译 34/34，功能 16/16（含 `game` 模块的游戏规则与
  SFX 合成套件，其中武器槽规则与多弹丸霰弹枪散射有专门用例）。

因此本环境的 `make test-all` 会在 `test-selfhost` 处停止，不能标记为全绿。

## 目录

```text
compiler/        编译器、汇编器、链接器、归档器与运行时
compiler-tests/  编译器、链接器和 Tinylibc 测试
include/         Toyc/Tinylibc 头文件
lib/             Tinylibc 源码
app/             示例与自托管应用
llm/             GPT-2、Qwen2 推理实现与共享数值基础设施
bootstrap/       版本控制内的自举种子
```

更细的语言特性记录见 [toyc-c-features.md](toyc-c-features.md)，种子说明见
[bootstrap/README.md](bootstrap/README.md)。
