# Rasterfall GB2312 16×16 点阵字库

`gb2312-16.rfh` 是 Rasterfall 的固定宽度屏幕字库。ASCII 字符为恢复的旧版 VGA 8×16 点阵，
GB2312 双字节字符为 16×16；运行时和地图排布图导出器都从同一文件读取，不依赖系统字体或
FreeType。

字形来自 WenQuanYi Bitmap Song 1.0 (Hero) RC1：汉字取 `wenquanyi_12pt.bdf`（16×16
strike）。半宽 ASCII 沿用加入中文字体库前的 Rasterfall VGA 8×16 字形，源表保存在
`source/vga8x16.inc`：

- 上游项目：https://sourceforge.net/projects/wqy/
- 上游归档：`wqy-bitmapsong-bdf-1.0.0-RC1_GPLv2+.tar.gz`
- 上游归档 SHA-256：`c29b2c2f5db73ff74d11a7dca9608414813da1c4240b7b6c829a58757b35ebe6`
- 字体版权：Copyright (c) 2004-2010, The WenQuanYi Project Board of Trustees and Qianqian Fang
- 许可：GPL v2 或更高版本，附字体嵌入例外；完整条款见 `COPYING`

仓库保存压缩的对应 BDF 源文件。确定性重建命令：

```sh
make generate-gb2312-font
```

`gb2312-16.rfh` 格式是小端 32 字节头、95 个 ASCII 字形、A1-F7/A1-FE 顺序的 GB2312
16×16 点阵及 Unicode 到字形槽位的有序索引。汉字每行用大端 16 位位图保存，最高位在左。

## 玩家界面无衬线字体

`ui-sans18.rff` 是独立的 **Rasterfall UI Sans 18** 字形缓存，供新版玩家 HUD、设备、通讯和终端使用。
原 GB2312 字库继续服务经典 HUD 与世界文字。派生字形来自官方
[Noto Sans CJK SC Regular，Sans2.004](https://github.com/notofonts/noto-cjk/blob/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf)，
Copyright 2014-2021 Adobe。上游及派生字库采用
[SIL OFL 1.1](https://github.com/notofonts/noto-cjk/blob/Sans2.004/LICENSE)，完整许可和版权随
`UI-SANS-OFL.txt` 一同分发。派生字体使用独立名称，不宣称由原作者背书。

仓库只保存 18 px 的预计算字形矩形缓存与元数据，不打包 16 MiB 的 OTF。字符集包含 ASCII、完整
GB2312 以及替代字形、省略号、方向箭头。20 px 字格、16 px 基线采用三档非零灰度 coverage；相邻
同覆盖率矩形在离线生成时合并。运行时一次加载，按 Unicode 索引并缓存热点查找，直接提交既有 canvas
矩形，不依赖操作系统字体、FreeType 或 CPU 全屏 UI 图像。未知字形回退 `?`；文件缺失时回退经典字体。

从上述官方 tag 下载原 OTF 到 `tmp/` 后可重建；构建环境使用 Pillow 12.3.0。原文件 SHA-256 和输出
SHA-256 见 `ui-sans18.json`，内容检查无需原始 OTF：

```powershell
python tools/fonts/build_player_ui_font.py --source tmp/ui-font-source/NotoSansCJKsc-Regular.otf
python tools/fonts/build_player_ui_font.py --check
```
