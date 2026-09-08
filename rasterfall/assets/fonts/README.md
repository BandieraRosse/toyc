# Rasterfall GB2312 16×16 点阵字库

`gb2312-16.rfh` 是 Rasterfall 的固定宽度屏幕字库。ASCII 字符为 8×16，GB2312 双字节字符为
16×16；运行时和地图排布图导出器都从同一文件读取，不依赖系统字体或 FreeType。

字形来自 WenQuanYi Bitmap Song 1.0 (Hero) RC1：汉字取 `wenquanyi_12pt.bdf`（16×16
strike），半宽 ASCII 取同包的 `wenquanyi_13px.bdf` 并放入 8×16 单元：

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
