# GPU 光照实验与验证

> 状态：当前操作指南
> 所有者：GPU Scene

## 建筑硬件光追与对照

默认自动探测设备能力，在支持 Vulkan 1.2 和 KHR Ray Query 时启用静态建筑硬件遮挡。
`RF_GPU_ARCHITECTURE` 可设为 `auto`、`hardware` 或 `software`；进程初始化时读取，切换需重启。
`hardware` 在不支持的设备上明确失败。日志须出现 `rf-gpu-ray: requested=... selected=...`，
硬件路径还会打印启用特性及建筑构建的三角形数、耗时、保留字节数。

```powershell
$env:RF_GPU_ARCHITECTURE='hardware'
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --gpu-lighting-test
python tools/outpost_lighting_check.py --output tmp/outpost-ray-hardware
$env:RF_GPU_ARCHITECTURE='software'
python tools/outpost_lighting_check.py --output tmp/outpost-ray-software
Remove-Item Env:RF_GPU_ARCHITECTURE
$env:RF_GPU_VULKAN_VENDOR_ID='10de'
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_outpost_perf.ps1 -CompareArchitecture -Views outpost-light-1f -Rounds 3 -Samples 240 -OutputDirectory tmp/outpost-ray-perf
```

捕获脚本当前固定 NVIDIA；跨厂商回退验证使用原生 `--gpu-lighting-test` 并指定 vendor。
性能脚本交替软件/硬件顺序，要求实际选中的后端匹配请求，保存同一 EXE/map 哈希。
性能采样关闭 validation，不开截图；同步与生命周期验证另行开启 validation layer，不能混算耗时。
光照回归包含空结构、替换、有限射线、双面、resize 后遮挡保留和无效输入不破坏旧结构。
只验证静态建筑加速，不据该结果宣称动态光追、间接光或全场景稳定帧率。

实现边界见[GPU 实时光照](../architecture/gpu-lighting.md)。先运行 Windows native build，再启动：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 build
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_lighting_lab.ps1
```

实验区位于前哨站模型实验场东侧，可从模型场地步行进入。脚本直接选择前哨站并把相机放在实验区入口；也可使用 `--renderer gpu-scene --map rasterfall/assets/maps/outpost.map --gpu-normal-scene lighting-lab 0`。

普通进入默认关闭，东北侧独立终端按 E 开启/关闭；显式 `lighting-lab` 镜头预先开启。展示开关与时钟遵守[实验区合同](../reference/experiment-labs.md)。

三排球体从近到远分别为 PBR 非金属、金属、风格化非金属。每排从左到右粗糙度为 0.90、0.64、0.38、0.12，底座标签数字表示百分数。两盏暖色/冷色聚光灯沿轨道移动，球体和侧边立柱提供投影遮挡。球体是 GPU 展示资源，不参与玩法碰撞；地面通过正常地图 surface/collision 声明提供行走空间。CPU 只显示地图场地。

大厅渲染终端可开启 GPU 手电筒、切换角色风格化材质与纹理过滤。实时阴影和 PBR 是 GPU 默认能力；CPU 不支持这些高级选项。

## 自动与实机检查

前哨站三层固定视图：先用 `NativeCodex.ps1 test` 或 `run` 完成构建和资源暂存，再运行
`python tools/outpost_lighting_check.py --output tmp/outpost-lighting-review`。
该工具逐个等待真实子进程，检查退出码、日志及 GPU 捕获，并保存 B1、一层、二层、楼梯和研究翼 PNG。
固定镜头通过 `--gpu-normal-scene outpost-light-b1|outpost-light-1f|outpost-light-2f|outpost-light-stairs|outpost-light-research 0`
进入，地图需选择 `rasterfall/assets/maps/outpost.map`。截图不作为性能结论；成本使用
`tools/gpu_outpost_perf.ps1 -Views outpost-light-1f -Rounds 3 -Samples 240` 单独采样。

两件原创灯具通过 `python tools/outpost_lights_assets.py` 沿 Blender → GLB → 原生导入器重建；
机器需安装脚本所配置的 Blender 路径，原生 asset-tools 已构建。源模型不提交，运行 RMESH、manifest
和生成器共同维护。重建布局用 `python tools/outpost_storeys.py --write`。
检查灯具的发光面、锥体方向、门洞通光、楼板隔光和转向时的连续性；RTS 剖切仍保留完整建筑遮挡。

`rasterfall.exe --gpu-lighting-test` 在物理 Vulkan GPU 上做离屏行为回归，检查太阳/聚光灯遮挡、移动点光源、粗糙度、风格化响应、旧烘焙乘数无效和 resize。它有显式诊断 readback，不能代替 native present。

`ARCHITECTURE LIGHT PASS` 另外检查 point、超过两个动态阴影名额的 spot、太阳、有限光线段
和清空旧世界遮挡；BOX 与三角形对照保护实体解析求交及共边遮挡。静态 BVH 只包含实际建筑几何，
不能据此声称所有动态物体均已投射局部灯阴影。限定原生证据见[本轮现场](../archive/outpost-lighting-20261005.md)。

同一入口的 `LIGHTING RANGE PASS` 检查无太阳/局部灯时，仅改变法线朝向就能区分天空与地面填充，
并验证自发光 HDR 4/8 经真实 half-float 目标与色调映射后仍有亮度顺序，未同时裁成纯白。
这些是稳定行为合同，不以指定截图颜色或亮度常量作为美术签收。阴影新增连续 PCF 与级联过渡后，
仍须现场观察斜面自阴影、接触处悬浮、转头与跨级别时的边缘变化；使用
[正常场景性能采样](rendering-performance.md#实验园区正常场景采样)核对 GPU 分位数和实际阴影绘制量。

正常实验区截图可给启动参数增加 `--frames 3 --gpu-frame-capture <绝对路径> --gpu-capture-frame 2`。按 [Windows Native](windows-native.md) 等待进程句柄退出并检查日志和生成的 `.scene.ppm`。固定 capture 时使用固定展示时钟；观看灯移动时不要启用 capture。

资源生命周期继续使用 `tools/gpu_scene_play.ps1 -Stage Interactive` 和 `-Stage World`。修改共享 CPU/Scene 展示来源后运行 `--logic-test`，确认 CPU 烘焙仍可用。开启 Vulkan validation 时必须检查 VUID 和同步错误；有 validation 的短帧时间不是正式性能数据。
