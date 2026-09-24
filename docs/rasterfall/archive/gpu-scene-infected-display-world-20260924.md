# GPU Scene 导入感染体展示模型现场（2026-09-24）

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收

Campaign `rasterfall.map` 的六条 `MODEL` 展示值（style 7/8、10/11、13/14）分别使用 block/humanoid 的 common、fast、heavy 导入资产。正常帧 Scene WORLD 现在按现行 idle rig 姿态、CPU skinning、材质和逐三角形冻结 V1 光照生成第十一类网格。旧渲染仍负责正常呈现；Scene 仅在审计路径的独立 color/depth target 绘制并读回。

本轮 `NativeCodex.ps1 test` 的逻辑回归检查 83 条 Campaign world 值中 60 条不透明、15 条 LABEL 延后、4 条透明，并确认新模型网格含超过 10000 个顶点。Windows package 的 `model-infected 0` 两帧原生诊断为 1058 draw（其中 14 actor）、首帧 984 upload/60 hit、次帧 0 upload/1044 hit，离屏覆盖 570555 像素。Scene 读回与 mixed 截图在前景模型三个采样点 RGB 完全一致，另一处身体采样点各通道最大差 1。`near 0` 为 993 draw，地图及静态实例 979 draw。

证据保存在 `tmp/scene-infected-captures-20260924/`，包括 mixed BMP、Scene PPM 与日志。此现场只证明六种静态展示模型的资源及可见像素路径；动态敌人、透明项、LABEL、其余角色和正常帧 Scene present 仍按活动计划继续。

完整原生套件保存在 `tmp/scene-infected-native-sync-20260924/`：15 项运行均通过，manifest 为 `PASS - available gates`、`validation_sync=PASS`、`requested_vendor=10de`。本次运行使用 RTX 3050 和 Khronos synchronization validation；正常帧仍由 mixed present。
