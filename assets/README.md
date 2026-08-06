# Toyc 资产 v0.1

`generated/` 保存仓库内提交的小型 toyc 运行时资产：`wall.ttex` 是 Rasterfall
使用的墙面纹理，`test*.ttex`、`test.tsnd`、`test.tmesh` 是 `validate-assets`
校验的最小测试资产，`sfx_*.tsnd` 是 Rasterfall 的程序合成音效（见下）。
`maps/` 保存运行时地图。原始来源（PNG/JPEG/WAV/OBJ）和大型中间文件不随仓库
分发，见根目录 `.gitignore`；需要重新生成资产时，先在本地准备来源文件，再用
`build/toyasset convert` 转换。`validate-assets` 自动发现并校验
`generated/` 下全部文件，新增资产无需改动测试脚本；离线工具用法见
[tools/README.md](../tools/README.md)。

### 程序合成音效

`sfx_*.tsnd` 是 Rasterfall 的 8 种核心音效，由 `tools/gen_sfx.c` 链接
`lib/game/sfx.c` 引擎离线渲染：44100Hz 单声道 PCM16（从引擎立体声输出下混取
均值）。合成参数与运行时逐字节一致、输出确定性可复现，无外部来源文件。
Rasterfall 启动时加载这些资产并以 `toy_sfx` 样本模式播放（资产率与音频
线程输出率一致，无需重采样）；加载失败的音效回退程序合成，背景音乐仍为
运行时合成。

| 资产 | 音效 | 说明 |
|---|---|---|
| `sfx_gunshot.tsnd` | GUNSHOT | 枪声：噪声 + 平方衰减 + 110→55Hz 低频炮膛声 |
| `sfx_dry_fire.tsnd` | DRY_FIRE | 空枪：短噪声 |
| `sfx_reload_start.tsnd` | RELOAD_START | 换弹开始：双咔嗒（1100Hz 方波） |
| `sfx_reload_done.tsnd` | RELOAD_DONE | 换弹完成：单高咔嗒（1300Hz） |
| `sfx_hit_marker.tsnd` | HIT_MARKER | 命中：1500Hz 高频短音 |
| `sfx_kill.tsnd` | KILL | 击杀：噪声冲击 + 180→55Hz 下扫 |
| `sfx_bite.tsnd` | BITE | 被咬：90→45Hz 下扫 + 二次谐波 |
| `sfx_death.tsnd` | PLAYER_DEATH | 玩家死亡：380→45Hz 长下扫 + 泛音 |

重新生成（从仓库根目录执行）：

```sh
make generate-assets    # 构建 build/gen_sfx 渲染全部 sfx_*.tsnd，随后校验
build/gen_sfx           # 只渲染，默认输出 assets/generated/（目录须存在）
build/gen_sfx <目录>    # 渲染到指定目录
```

合成参数即 `lib/game/sfx.c` 的 `sfx_specs` 表；修改引擎后运行
`make generate-assets` 即可同步资产。

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
