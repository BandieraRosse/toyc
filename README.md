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
（见 `.gitignore`）。rasterfall 的 8 种核心音效（`assets/generated/sfx_*.tsnd`）
由 `make generate-assets` 链接 `lib/game/sfx.c` 引擎离线渲染，输出确定性可复现，
无需外部来源文件；rasterfall 启动时加载播放，缺失时回退程序合成。游戏运行时
只读取带 magic、版本和显式小端字段的格式，不解析
PNG/JPEG/WAV/OBJ 容器。格式说明见 [assets/README.md](assets/README.md)。
v0.1 不包含压缩、FBX、glTF、骨骼动画或 GUI 编辑器。

FPS 的 v0.2 纹理切片默认加载 `assets/generated/wall.ttex`，将 nearest RGB888
采样用于墙、地板和箱体；`--no-textures` 切回纯色，`--texture-stats` 输出纹理
三角形、像素和占位纹理统计。每 5 秒向终端输出一次性能统计（退出时再输出全量
汇总，`--no-stats` 关闭），字段含义：

- `fps`/`wall_us`：平均帧率与帧间隔。`wall` 是相邻两帧渲染起点（begin_frame）
  之间的墙钟时间，按循环迭代累计除以渲染帧数，精确等于 1e6/fps；它由活跃渲染
  时间（`frame_us`）与迭代间开销（`wait_us`）构成，与两者严格对账——平均帧率
  与渲染耗时的缺口就是 `wait` 的去向（组合器节拍/双缓冲背压/轮询调度）。
- `frame_us`（均值/p95/p99/max）：单帧渲染流水线活跃时间（begin_frame 到
  present），分位数反映掉帧风险。
- `wait_us`：由 `wall − active` 推导的迭代间开销（事件轮询、逻辑调度、双缓冲
  背压），不直接测量以保证对账严格；`stall_ms` 是其组合器背压部分（等组合器
  释放 buffer）。
- 阶段表 `logic/begin/scene/enemies/raster/overlay/present`：各阶段平均每帧的
  三角形、顶点、像素与耗时（及占比），用于定位热点阶段。
- 像素漏斗 `funnel`：包围盒扫描像素 → 覆盖测试通过像素（三角形覆盖效率）→
  深度通过像素（实际写屏量），两段比例揭示覆盖浪费与过度绘制。
- 路径拆分 `path`：纯色/纹理两条光栅化路径的三角形、像素与耗时，区分两者成本。

可用 `build/rasterfall --frames 300 --texture-stats` 在 Wayland 环境预览，
`build/rasterfall --logic-test` 为无窗口回归预览。

Rasterfall 的第一阶段 UDP 联机可用以下命令启动：

```sh
build/rasterfall --host --port 28460
build/rasterfall --connect 127.0.0.1 --port 28460
build/rasterfall --net-test              # 无窗口协议与 localhost UDP 回环
```

当前阶段由主机校验远端移动，并在权威敌人世界上执行远端切枪、换弹、射击、命中和
地图互动；双方能够看到队友模型，客户端带有位置预测与校正；敌人位置、生命和死亡
状态也由主机快照同步。敌人事件/音效复制和断线重连仍待下一阶段接入，当前属于可实机
验证的合作原型；主机产生的远端射击/换弹/受击事件也会转发给客户端音频，远端玩家的
生命与死亡状态由主机裁决。
连接超过约 3 秒没有收到对端包时，HUD 会显示断线；客户端会每秒发送 HELLO 尝试恢复，
主机会释放失联的客户端槽位以允许重新接入。

联机模式右上角会显示 `HOST`/`CLIENT`、对端连接状态和实时 RTT；主机在等待客户端时
显示 `WAITING FOR PLAYER`。

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
