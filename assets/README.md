# Toyc 资产工厂 v0.1

`sources/` 保存可再生成的原始或小型测试输入，`manifests/` 保存转换记录，
`generated/` 保存构建产生的 `.ttex`、`.tsnd`、`.tmesh`，`licenses/` 保存第三方
资产或工具的许可证说明。生成文件不应手工编辑，也不提交到版本库。

字段布局见 [formats.md](formats.md)；所有整数都是小端序，头部由逐字段写入组成，
不直接序列化带 padding 的 C 结构体。

转换器由 GCC 编译：

```sh
make assets
make validate-assets
build/toyasset inspect assets/generated/test.ttex
```

v0.1 只支持 8-bit、非隔行 PNG（灰度/灰度 alpha/RGB/RGBA）、baseline JPEG
（SOF0，8-bit，灰度或 YCbCr 4:4:4/4:2:2/4:2:0；渐进式等变体拒绝）、PCM
little-endian 16-bit WAV，以及三角面静态 OBJ。PNG/JPEG/WAV/OBJ 只在离线转换
阶段出现，游戏运行时只读取带版本和显式字段的自有格式。测试 PNG/JPEG 以 `.b64`
保存，避免在源码树中手工维护二进制文件；它们由 Makefile 解码后再转换。
`sources/wall.jpg` 是 Poly Haven “Square Brick Floor” 的 1024×1024 砖纹漫反射图，
按项目用户确认采用 CC0 1.0。`sources/wall_stylized.png` 是用于游戏的低细节风格化版本，
构建时缩放为 128×128 后写入 `generated/wall.ttex`。来源 URL 和转换记录见
`licenses/wall-source.txt`；CC0 不要求署名，但保留来源链接用于溯源。
