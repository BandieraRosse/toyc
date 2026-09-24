# GPU Scene SIGN 牌体与世界文字现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

冻结的 Runtime Map render 值现在生成第八类 SIGN 网格：牌柱、牌面及正反两面的
bitmap 字形。字形沿用旧路径的水平连续像素 run 合并及世界坐标，两面均参与世界
深度；牌体和字形按旧路径在每个三角形中心采样 V2 光照。

RTX 3050 Laptop GPU（vendor `10de`）原生证据在
`tmp/scene-sign-fixture-sync-20260924/manifest.json`。package executable SHA-256 为
`E2073D5EDBF07EB6665542AB8C23BCD2A21C507F67DE5B19C1D4215B626DBB8A`；
结果 `PASS - available gates`，`validation_sync=PASS`。120 帧 Scene 生命周期、
五类故障、pose、逻辑、Campaign normal-native、显式加载渲染 fixture 的
normal-map-wall 与 normal-map-sign 均通过。fixture 的 11 条 render 值中，
8 条进入不透明网格，6 个模型产生 28 draw；同代 GPU cache hit 和旧代退休均为 28。
Campaign near 0 的 83 条 world 值中，46 条进入网格或地面，29 条可见项暂缓、
4 条透明；离屏 WORLD 第一帧 747 draw、524,413 有效像素、727 upload 和
20 cache hit，次帧 747 hit、0 upload。

显式 `--map rasterfall/assets/maps/gpu_scene_render_fixture.map` 配合
`--gpu-normal-scene map-sign 0 --gpu-frame-capture tmp/scene-sign-fixture-20260924.bmp
--gpu-capture-frame 1 --frame-audit`，生成 mixed BMP 和同帧 Scene WORLD PPM。
离屏图的牌面与 `WORLD_SIGN` 字形可读，Scene probe 为 28 draw、475,506 有效像素。
对牌面矩形 x=553..726、y=154..257 做 RGB 核对，18,096 像素中 17,649 像素完全相同；
其余 447 像素包含边缘或不同背景，不能当作整个世界画面的差分通过。
这两张图在 `tmp/scene-sign-fixture-20260924.bmp` 和同名 `.scene.ppm`，
Scene PNG 仅用于查看，位于 `tmp/scene-sign-fixture-20260924.scene.png`。

早先脚本使用 `map-sign`/`map-wall` 镜头而未传入 fixture 地图，实际加载默认 outpost。
本轮已把 `--map` 写入原生专项，不能用先前默认地图的镜头结果证明 SIGN。

正常画面仍由 mixed 呈现；Scene 诊断在 mixed present 后提交并读回，
缺少天空、角色、透明层、VIEWMODEL 和 HUD，因此不是完整 Scene 正常帧证据。
