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

RFCHAR importer 的 `--position-scale` 选择局部每米格数。512 是旧默认；近景角色可显式用
65536（约 0.0153 毫米一格），8192 留作中间对照。顶点、骨骼 rest 与 socket 平移使用同一单位；
RFM2 的既有 `position_scale` 记录单位，Scene body 在 GPU 投影前换到 RFU。玩法仍是 512 RFU/m。
不要只放大顶点或缩小展示比例来补偿。旧资产需要从 GLB 重新导入，shader 无法恢复已合并的坐标。

`--material-roles <json>` 可显式将 GLB 完整材质名映射到既有的
`none/face/eyes/hair/skin/clothing/equipment` visual role；写入 RFM2 已有 byte 36，
没有新增材质格式。此文件是旧格式导入配置，不代表拟议的 `rf_material`/RFM2 v15 合同已经实现。
未知名称和角色拒绝；渲染器不按角色名、材质序号或颜色猜测角色类型。
基础颜色因子从 glTF 线性值按标准 sRGB 编码到现有 RGB24 存储。

## 原生三视图

先通过 `windows/NativeCodex.ps1 test` 或 `run` 更新运行目录。调用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_character_fidelity.ps1 -Model tmp/character-fidelity/rf_c01-65536.rmesh -OutputDirectory tmp/character-fidelity/native
python tools/rf_character_fidelity_report.py tmp/character-fidelity
```

使用正常 Outpost Scene 和目录 body 资源链，冻结 bind pose，在正面、45°、侧面分别捕获
无光照、材质 ID、原光照、平滑、柔和和按 visual role 分配的材质模式。`parts` 按导出材质分色；
如果脸与身体在导出时已经合批为同材质，它们显示同色，不能冒充逐对象分色。
当前 V22h GLB 没有纹理，所以无光照组只验底色；不得据此宣称 UV/纹理已经验收。

脚本参数以 `Get-Help tools/gpu_character_fidelity.ps1 -Detailed` 为准。
`-Depth quantized` 只恢复倒数深度量化；`-Depth legacy` 同时恢复旧整数变换，要求 512 格资产。
`-Reverse` 交换角色 draw 顺序；`-Distances` 改变观察距离；`-Views orbit -Frames 240`
运行逐帧缓慢绕转并拉远，保存末帧及全部 native 日志。需要观察中间角度时分别捕获对应帧数。
每次对照固定资产、显示、距离和帧数。退出码、`SCENE-NATIVE`、PPM 和哈希共同组成证据。

进程环境开关用于定向复现：`RF_GPU_CHARACTER_DISPLAY` 对应上述显示名，
`RF_GPU_CHARACTER_DEPTH` 为 `float/quantized/legacy`；`RF_GPU_CHARACTER_VIEW` 为
`front/quarter/side/orbit`，`RF_GPU_CHARACTER_DISTANCE` 使用 RFU；
`RF_GPU_CHARACTER_MODEL` 是预览模型绝对路径，`RF_GPU_CHARACTER_FREEZE=1` 冻结 bind，
`RF_GPU_CHARACTER_REVERSE=1` 交换顺序。它们不改变 Game/session。

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
  `RF_GPU_TEXTURE_FILTER=linear` 可设置启动值。当前角色 Scene 仍不接收纹理/MASK，这不是角色纹理签收。

高级开关默认关闭，运行中设置只影响后续 draw；无需重建角色资源。深度精度修正是 Scene 基础正确性，
默认生效，与高级光照开关独立。它使用 D32 原生 reversed Z（64/z），保留齐次近裁剪；
屏幕空间层和兼容诊断保持各自深度合同。

技术依据：[Vulkan 深度](https://docs.vulkan.org/guide/latest/depth.html)、
[片元深度写入](https://docs.vulkan.org/spec/latest/chapters/fragops.html)、
[glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)。

V22h 的一次具体归因、结果及未覆盖项见[2026-09-30 现场记录](../archive/character-fidelity-20260930.md)。
