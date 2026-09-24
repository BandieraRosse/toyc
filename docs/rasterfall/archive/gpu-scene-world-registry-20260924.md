# GPU Scene 地图资源代际持有现场

> 状态：历史现场归档；尚未进入 Scene GPU draw
>
> 日期：2026-09-24

`rf_gpu_scene_world_resources_prepare` 将四类已支持的不透明地图网格注册进独立 Scene world
registry，以 world/map generation 和逐项展示状态决定复用或重建。注册前先构建模型并检查可用
slot；旧资源失效后，已 pin 的 handle 保留到帧退休。正常帧审计现在持有这些 handle，而不是每帧
构建并立即释放网格。

Windows native 逻辑回归覆盖同代复用、air gate 展示切换重建、跨 world 代际失效、旧代 pin
存活与退休后释放。`NativeCodex.ps1 test` 通过。RTX 3050 near 0、固定 tick、3 帧原生审计
退出码为 0；首帧记录 `world_items=83 map_payload=83 map_resources=3 map_loads=3`
及 `map_opaque=16 map_primitives=29 map_deferred=59 map_transparent=4`，
最终 `GPU-FRAME attempted=3 rendered=3 cpu_fallback=0`。

Campaign 的四种受支持类别中只有三种生成非空模型。当前网格仍未接 GPU resource cache、
Scene WORLD draw 或正常帧 Scene 选择；画面由 mixed executor 呈现。
