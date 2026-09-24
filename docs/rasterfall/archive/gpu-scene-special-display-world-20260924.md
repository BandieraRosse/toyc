# GPU Scene 特殊感染体展示模型现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

Campaign 地图 `MODEL` style 3、4、5 分别对应 smoker、charger、tank 的静态
rig 展示。Scene 第十类生成网格从冻结 render 值取得来源位置和 style，复用旧
`enemy_rig_profile`、静态姿态求值、层级变换、八边棱柱形体与逐面 form light。
静态姿态输入在构建时显式初始化，不读取敌人观察器或展示捕获开关。
每个生成三角形再从冻结 V1 诊断光照场取中心样本。动态敌人的玩法和姿态仍由
原有路径所有；此切片仅处理地图里的静态展示模型。

RTX 3050 Laptop GPU（vendor `10de`）原生证据位于
`tmp/scene-model-special-final-20260924/manifest.json`。package executable SHA-256 为
`3C8CDFEF969EF4E38268092052315496D5084ACF9C6339731F697F1E12FA0274`；
结果 `PASS - available gates`，`validation_sync=PASS`。120 帧生命周期、五类故障、
pose、逻辑、Campaign normal-native、fixture map-wall/map-sign，以及 Campaign
model-legacy/model-special 镜头均通过。

near 0 的 83 条 world 值中，54 条进入地图网格或地面，21 条可见项暂缓，
4 条透明；特殊展示类新增 151 个 map primitive。离屏 WORLD 首帧 957 draw、
524,824 有效像素、937 upload 和 20 cache hit，第二帧 957 hit、0 upload。
model-special 镜头首帧 993 draw、772,036 有效像素，第二帧 993 hit、0 upload。

定向镜头的 mixed BMP 和 Scene WORLD PPM 位于
`tmp/scene-special-final-capture-20260924/special.bmp` 及同名 `.scene.ppm`。
中央展示模型躯干的 x=640..839、y=250..459 区域有 42,000 像素，
其中 41,967 像素 RGB 完全相同。天空、其他 actor、VIEWMODEL 和 HUD 尚未在
这张 Scene WORLD 目标中绘制，因此该局部结果不代表完整帧等价。

正常画面仍由 mixed 呈现；Scene 在 mixed present 后离屏提交并读回。
