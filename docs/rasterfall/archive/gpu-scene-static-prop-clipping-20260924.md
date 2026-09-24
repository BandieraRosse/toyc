# Scene 静态 RMESH 硬件裁剪现场（2026-09-24）

> 状态：历史现场归档；离屏 WORLD 诊断，不是阶段 2 退出或正常 Scene 呈现验收

当前契约见 [GPU 渲染架构](../architecture/gpu-rendering-architecture.md)，复现入口见
[Scene fixture](../guides/gpu-scene-fixture.md)。本切片将旧整数光栅的屏幕投影范围与顶点变换安全分开：
静态实例超出前者时可选择 Scene 硬件裁剪；旋转、缩放、camera 变换仍必须通过整数运算范围检查。

RTX 3050 原生验证命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_native.ps1 -OutputDirectory tmp/scene-prop-clipping-final-20260924 -ValidationLayerDirectory tmp/scene-validation-tools/mingw64/bin -DeviceVendor 10de
```

该目录 `manifest.json` 记录 19 项运行、`PASS - available gates` 和 `validation_sync=PASS`。
package executable SHA-256 为 `32748DBBB1E340ED19DD53234FD1D480884C4A7F0F143FC51FB97E35B835CF8D`。
逻辑、pose、120 帧生命周期、增长、resize、世界退休、五类故障及固定镜头均通过；清理后资源计数归零。
新增大三角形用例在 960×540 下恒深度和近裁剪分别覆盖 518400 像素，颜色及深度检查通过。

| 镜头 | 静态实例数值暂缓（前 → 后） | Scene draw（前 → 后） | 次帧地图 cache hit |
| --- | ---: | ---: | ---: |
| actor-standard | 8 → 0 | 1218 → 1240 | 978 |
| actor-rifleman | 4 → 0 | 1233 → 1249 | 987 |
| near 0 | 0 → 0 | 1241 → 1241 | 979 |

各镜头次帧地图上传均为零。`normal-actor-standard.bmp.scene.ppm` 与
`tmp/scene-transparency-final-20260924/` 的同名文件逐字节一致；原有角色、旗帜、投射物和交互物像素门槛通过。
这些保守可见性镜头中的新增提交未改变该截图，因此仅记录覆盖和预检缺口收敛，不声称修复了可见缺物。
近裁剪可见性由独立三角形用例证明；完整画面容差合同和动态敌人仍待完成，正常呈现仍为 mixed。
