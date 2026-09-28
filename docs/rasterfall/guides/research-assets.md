# Research 资产生成与验收

> 状态：当前操作指南

资产与摆放合同见 [Research Renovation V1](../reference/research-renovation-v1.md)。
生成器复用工业 Builder、GLB exporter 和统一 importer；无新资产格式。

## Windows native

```powershell
$env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:Path"
make -f windows/Makefile asset-tools
python tools/research_round.py --generate --blender 'E:\Blender 5.2\blender.exe'
python tools/map_layout_export.py rasterfall/assets/maps/outpost.map --output-dir tmp/research-layout
python tools/map_layout_query.py tmp/research-layout/output.json get BX18
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 package
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
```

Blender 路径按本机安装调整。layout 需要 Pillow。生成器验证米制尺寸、底部 pivot、identity
node、非退化面、预算和四材质上限；importer 验证最终 RMESH 再安装。
省略 `--generate` 可仅重新导入。Linux 辅助验证使用根 Makefile 的地图回归和 logic-test；
Windows 实机结论按 [Windows Native](windows-native.md) 获取。

## 画面

`--model-static-views <绝对RMESH路径> <绝对输出目录>` 提供逐件四视图。
Outpost 的 `--environment-capture <绝对输出目录>` 增加 `research-bx18-entry`、
`research-bx18-center`、`research-bx18-cluster`、`research-bx18-core`、
`research-bx18-prototype`、`research-bx18-rts` 六个正常 world renderer 镜头。
从 package root 运行，显式指定 `--map rasterfall/assets/maps/outpost.map`。

独立 Scene 定向镜头：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --map rasterfall/assets/maps/outpost.map --renderer gpu-scene --gpu-normal-scene research-bx18 0 --frames 5 --frame-audit --gpu-frame-capture C:/absolute/output/research.bmp --gpu-capture-frame 3
```

按真实进程退出码与日志验收；独立 Scene 输出为 `research.bmp.scene.ppm`。
核对无静态 prop deferred、无旧命令/mixed/bridge，以及 native 提交成功。
检查门口识别、Core 可见性、机柜正面差异、原型夹具、留白与入口净空。
模型四视图不能替代房间内比例和距离检查。
