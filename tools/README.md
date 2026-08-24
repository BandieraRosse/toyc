# Tools — 宿主编译的离线工具

以下工具用 GCC 构建、只在离线流程中使用，不进入运行时。统一在仓库根目录下
用 `make` 构建；Rasterfall 运行时资源位于 `rasterfall/assets/`。

| 工具 | 源码 | 构建 | 用途 |
|---|---|---|---|
| toyasset | `tools/toyasset.c` + `jpg_decode.c` | `make build/toyasset` | 离线格式转换 |
| gen_sfx | `tools/gen_sfx.c` + `rasterfall/lib/sfx.c` | `make build/gen_sfx` | 程序合成音效 → TSND |

## PMX 模型一键导入

```sh
tools/import-pmx-model.sh <模型目录> [模型名]
tools/import-pmx-model.sh --force <模型目录> [模型名]
```

脚本递归查找目录中唯一的 `.pmx`，使用 `make -j` 构建转换工具，然后生成
`rasterfall/private-assets/models/<模型名>.rmesh` 和同名 `.textures/` 目录。
PNG、BMP、SPA/SPH、JPG/JPEG 纹理会自动转为 TTEX。默认模型名来自 PMX 文件名；已有输出
不会被覆盖，除非显式指定 `--force`。导入日志会逐材质解码 drawing flags，并输出
几何、纹理、透明度、sphere、toon、edge、光照及蒙皮数据的 feature summary。

`jpg_decode.{c,h}` 是 toyasset 的 JPEG 解码依赖，不是独立程序。

## RFM2 mesh LOD

```sh
make lod-ar15
tools/rmesh_lod.py input.rmesh output_lod1.rmesh --ratio 0.4
```

`rmesh_lod.py` 对每个材质 primitive 做确定性的蒙皮/UV 感知顶点聚类，保留原始
顶点表、骨骼、动画和材质数据，只重建简化索引。`name_lodN.rmesh` 在运行时共享
`name.textures/`，避免为各级 LOD 复制纹理。输出三角形比例是目标值；为了保护
UV 接缝和骨骼边界，实际最接近比例会随模型拓扑略有变化。

## toyasset — 资产转换

```sh
build/toyasset convert <png|png1024|bmp|jpg|jpg128|wav|obj> <输入> <输出>
build/toyasset inspect <文件>
```

- 离线转换：PNG/JPEG → `.ttex`，WAV（PCM 16-bit 单/双声道）→ `.tsnd`，
  OBJ（三角面）→ `.tmesh`；`jpg128` 额外把图片缩放到 128×128
- 游戏运行时只读取转换后的格式，不解析 PNG/JPEG/WAV/OBJ 容器
- 无参数运行打印 usage 并返回 2
## gen_sfx — 程序合成音效资产

```sh
make generate-assets         # 重建全部音效资产
build/gen_sfx                # 只渲染，默认输出 rasterfall/assets/audio/
build/gen_sfx <输出目录>      # 渲染到指定目录（目录须存在）
```

- 链接 `rasterfall/lib/sfx.c` 引擎，把 Rasterfall 的 8 种核心音效离线渲染为
  44100Hz 单声道 PCM16 的 `sfx_*.tsnd`
- 合成参数即引擎内 `sfx_specs` 表；输出确定性可复现（固定噪声种子），
  修改引擎后重跑即可同步资产
- Rasterfall 启动时加载 `rasterfall/assets/audio/` 下这些资产并以 `toy_sfx` 样本模式播放；加载失败的
  音效回退程序合成，背景音乐仍为运行时合成
