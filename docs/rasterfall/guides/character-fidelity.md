# Blender → RF 角色保真诊断

> 状态：当前
> 所有者：角色导入、Scene presentation、GPU graphics

先冻结导出的 GLB 和 SHA-256，再分开检查源几何、坐标存储、相机变换、深度和光照。
不要给所有眼部表面加偏移或关闭深度测试。源中的实际穿插、重复面仍须在创作层处理。

## 同源检查

`tools/assets/rfchar_precision_audit.py <source.glb> --output <report.json>` 统计各材质的重复面、
源退化面及不同坐标格下的退化面；精确零面积计数也会包含球极点等微小拓扑，不能直接当作可见缺陷数。
`tools/blender/rf_character_surface_audit.py` 在 Blender 内按显式材质索引采样虹膜与遮挡层的前后关系；
正面平行射线不是全模型穿插证明，眼睑遮住虹膜边缘也可能是正确遮挡。

RF-C01 当前候选继承 V27 的单一不透明眼球表面，眼白、虹膜、瞳孔和小高光共用 UV0 基础色图；
旧独立虹膜与高光覆盖层已在 eyes 部件清单中显式移除。眼球外形、蒙皮和眼睑不变，
正常深度测试仍负责眼睑及头发遮挡。新结构不存在眼白与虹膜之间的独立深度竞争；
检查重点转为前侧多视角下的 UV、虹膜轮廓和眼睑边缘，不再对不存在的两层材质比较深度。

RFCHAR importer 的 `--position-scale` 选择局部每米格数。512 是旧默认；近景角色可显式用
65536（约 0.0153 毫米一格），8192 留作中间对照。顶点、骨骼 rest 与 socket 平移使用同一单位；
RFM2 的既有 `position_scale` 记录单位，Scene body 在 GPU 投影前换到 RFU。玩法仍是 512 RFU/m。
不要只放大顶点或缩小展示比例来补偿。旧资产需要从 GLB 重新导入，shader 无法恢复已合并的坐标。

`--material-roles <json>` 可显式将 GLB 完整材质名映射到既有的
`none/face/eyes/hair/skin/clothing/equipment` visual role；写入 RFM2 已有 byte 36，
该角色标记在 v14/v15 共用，独立于 `rf_material` 合同草案。v15 的窄范围 MAT1 表面
通过统一导入器 `--character-surface` 显式启用，见[资产工作流](asset-pipeline.md)。
未知名称和角色拒绝；渲染器不按角色名、材质序号或颜色猜测角色类型。
基础颜色因子从 glTF 线性值按标准 sRGB 编码到现有 RGB24 存储。

公开 RFCHAR fixture 可用 `tools/assets/test_rfchar_surfaces.py <fixture.glb>
--validator build-windows/glb-inspect.exe --runtime build-windows/rfchar-runtime-test.exe`
检查 v15 导入和实际 native loader 的边界。GPU 图像过滤可在构建 `rf-gpu-graphics-test.exe` 后用
`RF_GPU_TEXTURE_TEST=1` 定向运行；这些数值检查不替代角色实机图检。

## 原生三视图

先通过 `windows/NativeCodex.ps1 test` 或 `run` 更新运行目录。调用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_character_fidelity.ps1 -Model tmp/character-fidelity/rf_c01-65536.rmesh -OutputDirectory tmp/character-fidelity/native
python tools/rf_character_fidelity_report.py tmp/character-fidelity
```

使用正常 Outpost Scene 和目录 body 资源链，冻结 bind pose，在正面、45°、侧面分别捕获
无光照、材质 ID、原光照、平滑、柔和和按 visual role 分配的材质模式。`parts` 按导出材质分色；
如果脸与身体在导出时已经合批为同材质，它们显示同色，不能冒充逐对象分色。
有纹理的 v15 资产在无光照组保留基础色图，`parts` 则关闭图像采样以核对材质分区。
旧无纹理 GLB 的结果只验证底色。

脚本参数以 `Get-Help tools/gpu_character_fidelity.ps1 -Detailed` 为准。
`-Depth quantized` 只恢复倒数深度量化；`-Depth legacy` 同时恢复旧整数变换，要求 512 格资产。
`-Reverse` 交换角色 draw 顺序；`-Distances` 改变观察距离；`-Views orbit -Frames 240`
运行逐帧缓慢绕转并拉远，保存末帧及全部 native 日志。需要观察中间角度时分别捕获对应帧数。
每次对照固定资产、显示、距离和帧数。退出码、`SCENE-NATIVE`、PPM 和哈希共同组成证据。

`-Station 0/1/2/3/4` 分别观察静止、步行、持枪、瞄准、移动射击台位。
0/1 默认冻结 bind，`-Animate` 启用动作采样；2/3/4 始终使用正式持枪动作与完整握点求解。
`-Views right-quarter` 和 `right-side` 从另一侧观察扳机手，配合 `unlit` 检查深色手套与枪的接触。
持枪台位将观察中心下移至胸部，建议用 `-Distances 900 -Displays lit -Frames 120` 检查上身与枪。
各次固定帧捕获来自同一动作时钟，不通过截图位置反推游戏状态。
`-AimPitch`、`-AimYaw` 与 `-AimDistanceRfu` 可复现方向和近距离收敛；参数及实际模型哈希进入 manifest。
例如在台位 3 用 `-AimPitch 75 -AimYaw 45` 检查高举瞄准，用 `-AimDistanceRfu 1024` 检查两米目标。
观察台位 2 的放低持枪、台位 4 的移动反冲，并从两侧检查袖口和手指；单张正面图不能签收穿模。

进程环境开关用于定向复现：`RF_GPU_CHARACTER_DISPLAY` 对应上述显示名，
`RF_GPU_CHARACTER_DEPTH` 为 `float/quantized/legacy`；`RF_GPU_CHARACTER_VIEW` 为
`front/quarter/side/right-quarter/right-side/back/orbit`，`RF_GPU_CHARACTER_DISTANCE` 使用 RFU；
`RF_GPU_CHARACTER_MODEL` 是预览模型绝对路径，`RF_GPU_CHARACTER_FREEZE=1` 冻结 bind，
`RF_GPU_CHARACTER_REVERSE=1` 交换顺序，`RF_GPU_CHARACTER_STATION=0..4` 选择观察台位。它们不改变 Game/session。瞄准诊断对应 `RF_GPU_CHARACTER_AIM_PITCH`、`RF_GPU_CHARACTER_AIM_YAW` 与 `RF_GPU_CHARACTER_AIM_DISTANCE`。

## Blender 参考

```powershell
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python tools/blender/rf_character_fidelity.py -- rasterfall/private-assets/source/characters/rf_c01/authoring/build/v022h/rf_c01-v022h.glb tmp/character-fidelity/blender
```

工具重新读取相同 GLB，不读取较新的 `.blend`，使用匹配的焦距、近裁剪、相机位置和 Standard 色彩管理。
RF 相机右向量与 Blender 默认相机的手性不同；参考相机显式反转 X 基向量以匹配 RF 投影，
不镜像源几何或骨架。Blender studio 使用独立灯光作为形体参考，不是 RF 基础漫反射的像素等价目标。
当前脚本服务无纹理 GLB 的底色、材质分区和 studio 参考；纹理资产需要另补材质节点与 UV 验证。

## 实机高级功能

前哨站渲染终端的“高级功能”页可随时切换：

- **角色柔和材质**：眼部保持底色，脸/皮肤使用柔和漫反射，其余角色材质使用插值法线漫反射。
  关闭恢复原三角形 flat 光照。只作用于 Scene actor body，不改变骨架或玩法。
- **纹理线性过滤**：静态 Scene 纹理在 nearest 与 bilinear/repeat 间切换。
  `RF_GPU_TEXTURE_FILTER=linear` 可设置启动值。角色与装备基础色图使用独立的 clamp/trilinear mip
  采样，不受此静态 repeat 开关控制；MASK 仍不支持。

高级开关默认关闭，运行中设置只影响后续 draw；无需重建角色资源。深度精度修正是 Scene 基础正确性，
默认生效，与高级光照开关独立。它使用 D32 原生 reversed Z（64/z），保留齐次近裁剪；
屏幕空间层和兼容诊断保持各自深度合同。

技术依据：[Vulkan 深度](https://docs.vulkan.org/guide/latest/depth.html)、
[片元深度写入](https://docs.vulkan.org/spec/latest/chapters/fragops.html)、
[glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)。

V22h 的一次具体归因、结果及未覆盖项见[2026-09-30 现场记录](../archive/character-fidelity-20260930.md)。
