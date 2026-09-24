# GPU Scene boundary wall WORLD 现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

Runtime Map 的 object 投影按 authored ID 与 `level->props` 顺序冻结为独立值帧，
逐项核对类型、坐标、朝向及尺寸。Scene world registry 持有该值帧，并以完整 prop 值参与
world/map、V2 light bake 代际的资源复用键。frame ID 更新不会使同代网格重建。
boundary wall 从冻结的 prop 值调用与 mixed 共用的网格算法，作为第六类 Scene world 资源；
其他 static RMESH 尚未进入 Scene。

RTX 3050 Laptop GPU（vendor `10de`）原生证据在
`tmp/scene-boundary-final-sync-20260924/manifest.json`。package executable SHA-256 为
`076F2FF2E5FEFE09952A81E6390453ADA978FA60D4AA6F2233603176B7C972F2`；
结果 `PASS - available gates`，`validation_sync=PASS`。120 帧 Scene 生命周期、五类故障、
pose、逻辑、Campaign normal-native 与 normal-map-wall 均通过。

| 正常帧审计镜头 | 首帧 | 第二帧 |
| --- | --- | --- |
| Campaign near 0 | 134 条 object 投影、21 条 boundary wall；663 draw、515,187 有效像素、663 upload | 663 cache hit、0 upload、515,187 有效像素 |
| V1 map-wall | 1 条 object 投影、0 条 boundary wall；12 draw、333,843 有效像素、12 upload | 12 cache hit、0 upload、333,843 有效像素 |

fixture 地图无 boundary wall，因此非空模型仍为五类，21 个 GPU draw；
重复 prepare 命中 21 次，旧代退休释放 21 个资源。共享源码的 WSL
`make rasterfall` 构建通过，仅作为编译检查。上述正常帧审计均在 mixed present 后
提交独立 Scene target 并显式读回，不能作为正常 Scene 呈现或产品帧性能证据。
