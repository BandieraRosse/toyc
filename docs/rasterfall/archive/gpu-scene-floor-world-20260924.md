# GPU Scene 分区地面 WORLD 现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

Runtime Map 地面由 ground/floor/border、地图范围、出生区颜色和 authored ground 策略合成
一个平面，颜色分区与 V2 光照细分由 `draw_partitioned_floor` 拥有。本次将该算法改为消费
显式地图值；正常 mixed 路径继续传入当前地图，Scene 路径传入冻结的范围、出生区和
`toy_map_draw` 值。冻结后的地图改动不改变 Scene 地面模型，逐帧 frame ID 也不触发资源重建。

Scene world registry 增加地面 handle，和四类既有地图网格一起按 world/map、V2 light bake
代际及展示内容复用。原生 fixture 的 11 条 world 项中，7 条进入五类模型，21 个 GPU draw
在离屏 WORLD 覆盖 362,486 像素；重复 prepare 命中 21 次，旧代退休释放 21 个资源。

RTX 3050 Laptop GPU（vendor `10de`）原生证据在
`tmp/scene-floor-complete-sync-20260924/manifest.json`。package executable SHA-256 为
`29DEEEA3B06C7A9F25343054E5487495D38C2CC5042F5903502F961838661952`；
`PASS - available gates`，`validation_sync=PASS`。120 帧 Scene、五类故障、pose、逻辑、
Campaign normal-native 与 normal-map-wall 均通过。
共享 Scene 模块移除了 freestanding 路径缺失的 hosted `qsort`、`memchr` 和分配调用；
WSL `make rasterfall` 构建通过，仅用作共享源码编译检查，不替代 Windows GPU 验收。
资源键逐项比较冻结的完整 `toy_map_draw` 值，同代颜色变化会重建地面及地图网格；
逐帧 frame ID 变化则不会使同一内容失去缓存命中。
Scene 排序改用 freestanding 可用的有界原地堆排序；逻辑用例覆盖逆序 world ordinal
及重复 ordinal 拒绝。

| 正常帧审计镜头 | 首帧 | 第二帧 |
| --- | --- | --- |
| Campaign near 0 | 191 draw、433,864 有效像素、191 upload | 191 cache hit、0 upload、433,864 有效像素 |
| V1 map-wall | 12 draw、333,843 有效像素、12 upload | 12 cache hit、0 upload、333,843 有效像素 |

这些数值包含诊断读回，不是产品帧性能。正常画面仍由 mixed 呈现；边界墙、static RMESH、
角色/附件、其余 WORLD、透明及后续画面层仍待 Scene 接入。
