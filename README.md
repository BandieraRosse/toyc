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
make self-app           # 用 build/toyc 构建 app/ 目录中的应用
make clean              # 删除 build/ 和 tmp/
```

也可使用 `make lib`、`make app` 或 `make app-<name>` 由 GCC 构建库、全部应用或单个
应用；`make self-app-<name>` 是 `app/` 内应用对应的自托管构建。Rasterfall 不属于
Toyc 兼容范围，以 GCC 构建为准。

### Rasterfall

Rasterfall 是一个使用仓库内 Tinylibc、由 GCC 构建的第一人称射击原型，包含软件渲染、程序合成
音效、本地运行和基础 UDP 联机。Linux 版本使用 Wayland、ALSA/WSLg 音频和仓库内的
freestanding 运行时；Windows 版本通过独立平台层使用 SDL2、Win32 线程/同步和
Winsock，不要求 Toyc 输出 PE/COFF。

模型、动画格式、重定向与 IK 的模块边界见
[`rasterfall/docs/animation-architecture.md`](rasterfall/docs/animation-architecture.md)。

Linux 构建和运行（从项目资源目录读取）：

```sh
make app-rasterfall
build/rasterfall
```

本地私有角色模型可用 `make lod-characters` 确定性生成中距离 mesh LOD；生成物保留在
`rasterfall/private-assets/`，不纳入版本控制，运行时缺失时自动回退完整模型。

旧的单文件内嵌方式仍可使用 `make rasterfall-embedded` 或
`make app-rasterfall-embedded`，输出为 `build/rasterfall-embedded`。

无窗口逻辑回归可使用：

```sh
build/rasterfall --logic-test
build/rasterfall --host --port 28460
build/rasterfall --connect 127.0.0.1 --port 28460
```

Windows 版本需要 Linux 主机上的 MinGW-w64、CMake、Ninja 和 SDL2 构建依赖。首次
构建先安装/准备依赖（SDL2 源码和交叉编译工具会缓存到 `.windows-deps/`）：

```sh
make win-deps
make win-rasterfall
```

如果依赖已经准备好，后续可直接使用增量构建：

```sh
make win-rasterfall
```

该目标只增量生成 `build/rasterfall.exe`，资源保持外置，因此链接速度不受私有模型大小
影响。发布时运行 `make win-rasterfall-package`，生成 `build/rasterfall-windows.zip`；压缩
包内只有一个 EXE，并原样保留本地开发项目中的 `rasterfall/assets/` 和
`rasterfall/private-assets/` 美术资源、模型、源纹理与动画目录。程序启动时会以 EXE 所在目录为资源根目录，
不依赖启动时的当前工作目录。也可以直接运行 `make -f windows/Makefile package`。
Linux 默认程序继续读取仓库资源目录，其 `rasterfall-embedded` 兼容目标仍可生成内嵌资源
程序。
修改源文件、头文件或 Rasterfall 资源后，Makefile 会按依赖关系增量编译和重新链接。

`bootstrap/` 保存版本控制内的种子二进制。它们用于阶段性的自举检查，不参与默认
`make`，而且可能落后于源码：

```sh
make update-bootstrap       # 有意更新种子；会修改跟踪的二进制
./bootstrap-selfhost.sh     # 种子 → stage 2，并运行自包含测试
./bootstrap-to-10.sh        # 验证 stage 2…10 字节级收敛
```

## 测试

```sh
make test               # 常规编译/运行测试
make test-selfhost      # 无 toyc_rt 的自包含测试
make test-source        # 编译器源码级测试
make test-error         # 诊断测试
make test-toyld         # 使用 toyld 的链接/运行测试
make test-toyar         # 归档器测试
make test-toyld-archive # 归档链接测试
make test-toyld-self    # toyld 两阶段字节一致性
make test-llm           # GPT-2 数值/前向传播测试
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

`make test-self-app` 是独立的自托管 App 冒烟测试；运行前先执行 `make self-app`。
它只检查已经存在的 `build/*_self`，缺少的程序会被跳过。

部分 syscall 和 procfs 测试依赖运行环境。只读根文件系统可能使 `renameat2` 返回
`EROFS`，容器中的 `/proc` 字段也可能不同；遇到这类断言时，应区分环境差异和编译器回归。

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
