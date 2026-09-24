# GPU Scene 地图渲染值与首批网格提取现场

> 状态：历史现场归档；阶段 2 开发现场，不是正常帧 Scene 验收
>
> 日期：2026-09-24

正常帧 `--frame-audit` 通过 `rf_gpu_scene_world_render_freeze` 按 Runtime Map authored render 顺序，
同时生成 V2 world 元数据和独立 V1 渲染值帧。后者按值复制 `toy_map_draw`，以 frame、world、map
generation 和逐项 ID、ordinal、可见性、alpha 与 snapshot 核对。测试修改来源地图颜色后，
已冻结的值保持原样；修改冻结项 ordinal 时验证拒绝。

现有 persistent map 的 wall、box、ramp、style 2 platform 网格构建已改为可消费单条冻结值。
几何来自值帧；构建时的顶点光照仍查询当前 world-light 状态，跨地图 generation 使用前须固定此依赖。
渲染 fixture 的逻辑回归确认四类均产生模型：6 条不透明、3 条暂缓、2 条透明。
Windows native `NativeCodex.ps1 test` 通过。RTX 3050 的 near 0、固定 tick、3 帧原生审计退出码为 0，
每帧记录 `world_items=83 map_payload=83 map_opaque=16 map_primitives=29 map_deferred=59 map_transparent=4`；
`path=gpu-native`，`GPU-FRAME attempted=3 rendered=3 cpu_fallback=0`。

上述网格仅在审计中构建并释放。59 条暂缓项包含现有其他地图/资产表示；透明项也未提交。
正常画面仍由 mixed executor 呈现，尚未完成 Scene 资源解析、WORLD 共深度绘制或完整帧接线。
