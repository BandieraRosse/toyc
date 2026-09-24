# 正常帧地图 Scene 离屏诊断现场（2026-09-24）

> 状态：历史现场归档；正常帧后离屏诊断，不是 Scene 呈现验收
>
> 日期：2026-09-24

正常帧 `--frame-audit` 已在 Core mixed present 后使用当帧冻结的 Runtime Map V1 渲染值和
Scene world registry handle，调用通用 GPU cache、pin、材质校验与 draw 编码器。独立 graphics
owner 在相同 Vulkan context 上绘制四类地图不透明网格，读回 Scene color/depth；owner 和 cache
跨审计帧复用，退出时先销毁 cache、graphics，再失效 world registry。

此入口是诊断：正常画面仍由 mixed executor 呈现。诊断在已有 `FRAME-AUDIT` 计时点之后运行，
其 readback 时间不包含在此前记录的 `whole_loop_ms` 中。Campaign near 0 镜头下首批四类网格
不进入视锥中的有效深度像素，因此另用 V1 `map-wall` 镜头检查可见覆盖。

验证使用 Windows 原生 package、RTX 3050 Laptop GPU（vendor `10de`、device `25e2`）。
`tmp/scene-normal-world-probe-sync-20260924/manifest.json` 记录 executable SHA-256
`0E703923EA7D1B4AE216116DE89E968047EE950BBE0C704168C75E933BA031E9`，
`PASS - available gates`、`validation_sync=PASS`；Khronos layer 加载且 Synchronization Validation
开启。120 帧 Scene fixture、五种 present 故障、pose、逻辑、三帧 Campaign normal-native、
两帧 normal-map-wall 全部通过。

| 正常帧诊断 | 首帧 | 后续帧 |
| --- | --- | --- |
| Campaign near 0 | 29 draw，29 upload | 第 2、3 帧各 29 cache hit、0 upload |
| V1 map-wall | 4 draw，覆盖 62,278 像素、4 upload | 覆盖 62,278 像素、4 cache hit、0 upload |

这证明同帧地图来源可穿过冻结值、handle、GPU cache 和离屏 WORLD draw。尚未证明正常
Scene present、完整 WORLD、透明/effects、VIEWMODEL 或 OVERLAY；这些仍按活动计划推进。
