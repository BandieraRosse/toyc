# 设施家具生成与前哨站验证

> 状态：当前操作指南

空间与资产合同见 [Outpost V1](../reference/outpost-hall-v1.md)。生成器复用现有 Blender
Builder、flat 材质与 GLB 检查，不使用第二套模型格式。GLB/Blend 保留在本地私有源目录，
公开 RMESH 与 manifest 可以由程序源重新生成。

## Windows 生成

先按 [Windows Native](windows-native.md) 确认 MSYS2 lane。当前原生离线工具目标用于无纹理
GLB 导入与 Runtime Map 检查，不包含有纹理资产所需的 toyasset 转换器。

```powershell
$env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:Path"
make -f windows/Makefile asset-tools
python tools/facility_round.py --blender 'E:\Blender 5.2\blender.exe'
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 package
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
```

Blender 路径按本机安装位置修改；脚本的 `--tool-dir` 可指定已编译转换器目录，Linux 使用
`build/`。原生诊断工具使用当前 runtime 的 exe-relative I/O，直接传地图时应使用绝对路径。
地图导出器会将输入路径解析为绝对路径，并在 Windows 使用 `build-windows/map-inspect.exe`。

```powershell
python tools/map_layout_export.py rasterfall/assets/maps/outpost.map --output-dir tmp/outpost-v1/layout
python tools/map_layout_query.py tmp/outpost-v1/layout/output.json summary
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --map rasterfall/assets/maps/outpost.map --environment-capture C:/Users/Legion/Desktop/toyc/tmp/outpost-v1/captures
```

布局工具需要当前 Python 环境可导入 Pillow。截图输出路径也按本机工作区修改。
显式前哨站地图的 environment capture 保留 Outpost identity，输出四张大厅视角及 `outpost-rts`、
`research`、`operations`、`power`、`control`、`test-yard` 六张扩建区 BMP，使用正常 world/actor renderer，固定 seed，不推进 simulation。
它验证构图与摆放；真实 present 另用 `--gpu-scene-play --map rasterfall/assets/maps/outpost.map`
启动，按 Windows 指南等待真实进程退出并检查日志。

逻辑回归检查大厅四处门洞、桌周环路、两间设施房和测试场可通行，桌子与外墙确实阻挡，以及基地没有可交互终端。
修改 placement 后重新导出布局；逐件资产可以使用 `--model-static-views` 做四视图检查。

Host 候选柜用 `tools/host_rack_layout.py` 写入前哨站地图，九件机柜资产由 `tools/host_rack_round.py` 生成；
资源映射和布局合同见 [Host Rack V2](../reference/host-rack-v2.md)。Outpost environment capture 额外输出
`host-racks.bmp` 与 `host-side.bmp`；逻辑回归覆盖满配八柜、两排通道和机架阻挡。
