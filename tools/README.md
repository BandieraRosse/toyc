# Tools — 宿主编译的离线工具

以下工具用 GCC 构建、只在离线流程中使用，不进入运行时。统一在仓库根目录下
用 `make` 构建；用法说明与资产格式见 [assets/README.md](assets/README.md)。

| 工具 | 源码 | 构建 | 用途 |
|---|---|---|---|
| toyasset | `tools/toyasset.c` + `jpg_decode.c` | `make build/toyasset` | 离线格式转换与资产校验 |
| gen_sfx | `tools/gen_sfx.c` + `lib/game/sfx.c` | `make build/gen_sfx` | 程序合成音效 → TSND |

`jpg_decode.{c,h}` 是 toyasset 的 JPEG 解码依赖，不是独立程序。

## toyasset — 资产转换与校验

```sh
build/toyasset convert <png|jpg|jpg128|wav|obj> <输入> <输出>
build/toyasset inspect <文件>
build/toyasset validate <文件>
```

- 离线转换：PNG/JPEG → `.ttex`，WAV（PCM 16-bit 单/双声道）→ `.tsnd`，
  OBJ（三角面）→ `.tmesh`；`jpg128` 额外把图片缩放到 128×128
- 游戏运行时只读取转换后的格式，不解析 PNG/JPEG/WAV/OBJ 容器
- 无参数运行打印 usage 并返回 2
- `make validate-assets` 自动校验 `assets/generated/` 下全部资产

## gen_sfx — 程序合成音效资产

```sh
make generate-assets         # 重建全部音效资产并校验
build/gen_sfx                # 只渲染，默认输出 assets/generated/
build/gen_sfx <输出目录>      # 渲染到指定目录（目录须存在）
```

- 链接 `lib/game/sfx.c` 引擎，把 wayland_fps 的 8 种核心音效离线渲染为
  44100Hz 单声道 PCM16 的 `sfx_*.tsnd`
- 合成参数即引擎内 `sfx_specs` 表；输出确定性可复现（固定噪声种子），
  修改引擎后重跑即可同步资产
- wayland_fps 启动时加载这些资产并以 `toy_sfx` 样本模式播放；加载失败的
  音效回退程序合成，背景音乐仍为运行时合成
