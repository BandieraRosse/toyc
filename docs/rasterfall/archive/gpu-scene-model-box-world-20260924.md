# GPU Scene 地图 MODEL 盒体 WORLD 现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

Runtime Map render 值帧中的普通 `TOY_MAP_DRAW_MODEL`（style 0）有 8 条。
旧路径将它们绘成世界盒体，使用显式 V1 诊断光照。Scene floor 值帧现在冻结该
32×24 光照场，第七类 Scene world 网格从冻结的 render 值生成相同五个面，
每面保持旧路径单四边形的三角划分，且按每个三角形中心写入恒定光照。
style 1–14 的展示模型仍待接入。

RTX 3050 Laptop GPU（vendor `10de`）原生证据在
`tmp/scene-modelbox-final-sync-20260924/manifest.json`。package executable SHA-256 为
`09A07CC8C7D1EA2C09670A1C624BB92D3AA1AEBE322D8EBB61BBCA1596F806CB`；
结果 `PASS - available gates`，`validation_sync=PASS`。120 帧 Scene 生命周期、
五类故障、pose、逻辑、Campaign normal-native 与 normal-map-wall 均通过。
共享源码 WSL `make rasterfall` 构建通过，仅作为非 Windows 编译检查。

Campaign near 0 的 83 条 world render 项中，45 条进入地图网格或地面分区，
30 条可见项暂缓，4 条透明。第七类模型盒体增加 20 个 GPU draw；
离屏 WORLD 首帧合计 740 draw、524,413 有效像素、720 upload 与 20 cache hit，
第二、三帧各 740 cache hit、0 upload。该镜头下模型盒体没有增加覆盖像素；
此项证明资源与 draw 接入，不证明其可见外观已完成截图差分。

另以 `--gpu-normal-scene mid 0 --frames 2` 对准旧地图中 `z=-7600` 附近的普通
模型桌面，原生进程退出码为 0；第一帧离屏 WORLD 为 750 draw、825,418 有效像素。
该总覆盖包含其他世界物件，仍不能单独归因于模型盒体，后续需要隔离画面验证。

正常画面仍由 mixed 呈现；本诊断在 mixed present 后向独立 Scene target
提交并显式读回，不是产品帧性能或完整 Scene 呈现证据。
