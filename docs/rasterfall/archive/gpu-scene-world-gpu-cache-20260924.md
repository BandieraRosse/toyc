# GPU Scene 真实地图网格 GPU cache 与离屏 WORLD 现场

> 状态：历史现场归档；独立诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

独立 `--gpu-scene-native-fixture` 在原有地图盒、RF rifleman 和 HEAD 的 native 三件套帧完成后，
加载 `gpu_scene_render_fixture.map`。Runtime Map 的 11 条 authored world 项按值冻结，
四类支持的不透明网格由 Scene world registry 持有，以同一 graphics owner 的 GPU resource cache
经可复用的 `rf_gpu_scene_world_gpu_prepare` 完成 pin、材质验证、上传与 draw 编码，
并提交到离屏 Scene WORLD color/depth。诊断读回确认非空覆盖；随后重复准备检查 cache hit，
再检查失效旧代在 pinned 时保留设备资源、帧退休后释放。

RTX 3050 定向 3 帧入口退出码为 0：`SCENE world-gpu=PASS world_items=11 opaque=6 models=4
draws=13 covered=183437 cache_uploads=13 cache_hits=13 retired_releases=13`。
完整同步验证现场位于 `tmp/scene-world-gpu-sync-20260924/`，manifest 记录
`result=PASS - available gates`、`validation_sync=PASS`、vendor `10de` 和 executable SHA-256
`B02B443B08896230D8DC0BF9A41169F3E3D07742D4598D0E1C4B6084730A1979`。
120 帧生命周期、五类故障、pose、逻辑用例及 3 帧 normal-native 全部通过；生命周期日志记录
`SCENE result=0 rendered=120 bridge=0`。normal-native 三帧每帧记录 `map_resources=3
map_loads=3 map_opaque=16`，证明同代网格没有反复构建；最终
`GPU-FRAME attempted=3 rendered=3 cpu_fallback=0`。

当前真实地图网格的 GPU draw 仅在隔离离屏诊断执行，并有显式 readback。正常帧仍使用 mixed
executor；其余 WORLD、透明、特效、VIEWMODEL 与 OVERLAY 尚未进入完整 Scene 提交。

将 GPU 准备抽到独立编译单元后，以该版本运行完整同步验证专项：
`tmp/scene-world-gpu-helper-sync-20260924/` 的 manifest 为 `PASS - available gates`、
`validation_sync=PASS`，executable SHA-256 为
`02DFFCCC8610059D01E9B37F086BDCFF9B3ED3D2549D552B7139CDADA2BC3AF5`。
120 帧生命周期、五类故障、pose、逻辑和 normal-native 均通过；`world-gpu` 仍为
13 upload、13 hit、13 retirement release 与 183437 覆盖像素。
