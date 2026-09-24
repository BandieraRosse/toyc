# GPU Scene legacy 展示模型与顶点光照现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

Campaign 的地图展示 `MODEL` style 1、2、6、9、12 现从冻结 render 值生成第九类
Scene WORLD 网格。style 1/2 与 style 6/9/12 的 legacy 家族共用现有方块人、圆柱人
形体目录；后者分别使用 common、fast、heavy 原有颜色，heavy 保持 1350 缩放。
鞋、躯干、头、脸、圆柱顶面等仍按旧形体顺序和世界坐标构建。每个三角形从冻结的
V1 诊断光照场取中心样本。style 3–5、7–8、10–11、13–14 仍待接入。

可见镜头发现生成网格原先经过整数深度兼容管线，该管线忽略顶点光照，
因此即使网格持有 V1 样本，Scene 仍只显示未增亮的材质原色。当前生成网格改用
读取顶点光照的 graphics 管线，静态 RMESH 保留整数深度兼容管线；
flat 材质顶点光照上限从 256 调为旧 Raster 的 384。
本机没有 `glslangValidator`，检查 `rf_graphics_frag_spirv` 中唯一的 256.0
浮点常量后，将对应 `OpConstant` 的值改为 384.0；同一改动更新 GLSL 源。
后续取得编译器时应重新生成并核对该 SPIR-V 文件。

RTX 3050 Laptop GPU（vendor `10de`）原生证据在
`tmp/scene-model-legacy-sync-20260924/manifest.json`。package executable SHA-256 为
`2C75FE89CC918ED4E2AACFB7407B5AD5ED1B6FE845D87FCDB42AC2B75F2DFDFD`；
结果 `PASS - available gates`，`validation_sync=PASS`。120 帧生命周期、五类故障、
pose、逻辑、Campaign normal-native、渲染 fixture 的 map-wall/map-sign 与
Campaign model-legacy 镜头均通过。

near 0 的 83 条 world 值中，51 条进入地图网格或地面，24 条可见项暂缓、
4 条透明；新增 legacy 展示类含 59 个 GPU cache draw。
离屏 WORLD 首帧 806 draw、524,824 有效像素、786 upload 和 20 cache hit，
次帧 806 hit、0 upload。model-legacy 镜头首帧 826 draw、716,353 有效像素；
第二帧 826 hit、0 upload。

定向 `model-legacy` 帧的 mixed BMP 和 Scene WORLD PPM 位于
`tmp/scene-model-legacy-vertex-20260924.bmp` 及同名 `.scene.ppm`。
style 1 方块人头部内的 x=540..739、y=90..289 区域有 40,000 像素，
其中 39,245 像素 RGB 完全相同；另 755 像素含边缘或背景差异。
这一局部核对不能证明完整 Scene 正常帧等价。

正常画面仍由 mixed 呈现；Scene 在 mixed present 后提交并读回。
