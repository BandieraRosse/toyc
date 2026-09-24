# GPU Scene 静态 RMESH WORLD 现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

Runtime Map object 值帧除 authored ID、投影顺序与完整 prop 外，冻结每个实例的
V2 世界光照 Q8 值。Scene world registry 按资产 profile 路径加载静态 RMESH，
与六类生成网格共用 generation、pin 和 GPU cache 生命周期。可见实例沿用 mixed 的
模型 AABB 检查、数值边界与材质解析；镜头外实例先剔除。

RTX 3050 Laptop GPU（vendor `10de`）原生证据在
`tmp/scene-rmesh-final-sync-20260924/manifest.json`。package executable SHA-256 为
`4E7C6B9BBEC72C84ADE54A507F7887E70CC4A7C3B85A6D0ABF8D605F899C9008`；
结果 `PASS - available gates`，`validation_sync=PASS`。120 帧 Scene 生命周期、
五类故障、pose、逻辑、Campaign normal-native 与 normal-map-wall 均通过。

Campaign near 0 的 134 条 object 投影中，21 条 boundary wall 进入生成网格；
其余 113 条 RMESH 实例中，98 条镜头外剔除，可见的 15 条产生 57 个 draw，
数值、材质和透明预检暂缓均为 0。23 个不同 RMESH 资产按路径共享 handle。
离屏 WORLD 首帧总计 720 draw、524,413 有效像素、700 upload 与 20 cache hit；
第二、三帧各 720 cache hit、0 upload。`near 60` 两帧补充诊断位于
`tmp/scene-rmesh-near60-20260924-042141/`，相同镜头下结果一致。
WSL `make rasterfall` 共享源码构建通过，仅作为非 Windows 编译检查。

这些审计都在 mixed present 后向独立 Scene target 提交并显式读回。
正常画面仍由 mixed 呈现；地图 model、label/sign、角色与附件、透明、effects、
VIEWMODEL 和 OVERLAY 尚未构成完整 Scene 帧。上述数值不证明产品帧性能收益。
