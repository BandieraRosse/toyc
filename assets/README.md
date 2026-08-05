# Toyc 资产 v0.1

`generated/` 保存仓库内提交的小型 toyc 运行时资产：`wall.ttex` 是 wayland_fps
使用的墙面纹理，`test*.ttex`、`test.tsnd`、`test.tmesh` 是 `validate-assets`
校验的最小测试资产。`maps/` 保存运行时地图。原始来源（PNG/JPEG/WAV/OBJ）和
大型中间文件不随仓库分发，见根目录 `.gitignore`；需要重新生成资产时，先在本地
准备来源文件，再用 `build/toyasset convert` 转换。

字段布局见 [formats.md](formats.md)；所有整数都是小端序，头部由逐字段写入组成，
不直接序列化带 padding 的 C 结构体。

校验工具由 GCC 编译：

```sh
make validate-assets
build/toyasset inspect assets/generated/test.ttex
```

v0.1 支持 8-bit、非隔行 PNG（灰度/灰度 alpha/RGB/RGBA）、baseline JPEG
（SOF0，8-bit，灰度或 YCbCr 4:4:4/4:2:2/4:2:0；渐进式等变体拒绝）、PCM
little-endian 16-bit WAV，以及三角面静态 OBJ。PNG/JPEG/WAV/OBJ 只在离线转换
阶段出现，游戏运行时只读取带版本和显式字段的自有格式。
