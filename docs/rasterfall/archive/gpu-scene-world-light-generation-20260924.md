# GPU Scene 地图网格光照代际现场

> 状态：历史现场归档；地图资源正确性回归，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

四类 Scene 地图网格在构建顶点时读取 Static World Lighting V2。此前 registry 只按
world/map generation 和展示状态复用网格；同一地图重新 bake 后可能保留旧顶点光照。

现在每次 V2 bake 递增 render context 的 light generation。`rf_gpu_scene_world_resources_prepare`
将它纳入复用键；逻辑用例固定同一地图和展示状态，只改变 light generation，检查四个
handle 重建、旧资源释放以及后续 pinned 旧代在帧完成前保持可解析。

Windows 原生 `NativeCodex.ps1 test` 通过。RTX 3050 的
`tmp/scene-light-generation-sync-20260924/manifest.json` 记录 executable SHA-256
`61C98DC53E3F35B4332F4275B59A8EC9A8D5A5C2CC78C6D60963BA42F23C24C7`，
`PASS - available gates` 与 `validation_sync=PASS`。120 帧 Scene fixture、五种故障、
pose、逻辑、Campaign normal-native 和 normal-map-wall 均通过。

此资源键只覆盖当前四类网格；其余 WORLD 内容和正常 Scene present 仍待接入。
