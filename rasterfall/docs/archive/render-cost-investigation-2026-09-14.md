# Campaign 敌人波次渲染开销调查（2026-09-14）

> 文档更新：2026-09-15
> 源码核对基线：`e5186b9` + Render Cost Investigation checkpoint 工作区。

用户现场：正常可保持 30 FPS，即使可见组件／模型较多；原战斗地图敌人约 30 个时掉至约
22 FPS。平台、现场相机与战斗 effects 尚未确认。以下为本地 Linux freestanding 软件渲染
实测，不声称重现同一现场，也不把 headless 等效 FPS 当作窗口 FPS。

## 方法与原始日志

`build/rasterfall --render-performance 12 --textures`，1280×720，默认 8 raster workers，
seed=1；保留当前 Campaign 地图、actor 和可用私有角色资源。固定近／中相机与
0/10/30/60 普通敌人（80% COMMON、10% FAST、10% HEAVY），每项预热两帧，计十二帧均值。
三轮串行，表格为各轮均值的中位数；不与构建并发。

日志：`tmp/render-cost-investigation/verified-run1.log` 至 `verified-run3.log`；
最终附加敌人模型内部计时的复测为 `final-detailed.log`。最早 `run1.log` 的 flat-planar
消融尚未正确接入，不用于结论。生成物不提交。

不含 begin、session、网络、真实战斗 effects、交互层、HUD、present、pacing 或截图 IO。
独立 world tick 对快照运行 16ms，复制在计时外；不会覆盖持续 AI 缓存、路径变化和真实 session。
同一消融的远近视角覆盖／遮挡不同，不宜按场景总体敌人数推算可见模型成本。

## 实测

单位 ms；各项消融保留其余正常 V2 consumer，均不是旧光照整套 A/B。

|视角／敌人|正常渲染|恒定 helper world sample|同细分、恒定平面光照|关闭 V2 平面细分／插值|legacy 普通敌人|跳过 AI actor|
|---|---:|---:|---:|---:|---:|---:|
|near / 0|24.257|23.165|17.308|15.108|22.639|15.454|
|near / 30|30.146|27.306|23.869|21.686|25.743|21.399|
|near / 60|39.763|37.094|35.389|32.559|29.914|32.189|
|mid / 0|30.160|25.785|19.035|16.889|29.263|20.662|
|mid / 30|35.743|35.018|29.511|25.930|31.956|29.678|
|mid / 60|48.987|43.011|37.298|35.435|33.309|38.857|

独立 world tick：near 0/30/60 为 0.158/1.794/3.253ms，mid 为
0.158/1.720/3.230ms。正常游戏 fixed step 为 16.667ms，每显示帧可能执行多个逻辑步，
不能把一次 tick 数值直接当作每显示帧逻辑开销。

30 敌人正常渲染分项：

|阶段|near|mid|
|---|---:|---:|
|世界提交（含 flags）|7.838|7.619|
|其中 sky/floor|4.064|3.909|
|其中 map draw records|0.208|0.236|
|其中 static props/gallery|2.240|2.252|
|其中 private fixture|0.654|0.664|
|敌人提交|4.270|5.720|
|AI actor 提交|6.175|5.827|
|flush 光栅墙钟（含分类／排序／等待）|11.540|16.180|

中位数分项不保证相加等于总耗时。near 30 敌人模型命令 8159，AI actor 命令 13349；
mid 为 11157 / 10098。bbox 扫描量 near 从无敌人 334 万增加到 752 万，
mid 从 935 万增加到 1037 万；敌人不仅有模型提交成本，还改变像素覆盖。

最终单轮内部计时（不是三轮中位数）：near 30 敌人约 4.183ms，
其中 skin 1.113、vertex 0.824、body triangles 1.659、bone hierarchy 0.026；
mid 30 约 5.688ms，其中 1.603/1.154/2.297/0.033ms。
这些差值计时排除模型求值入口之外的姿态适配／shadow 等；不得相加声称完整拆分。

## 源码原因与判断

1. **V2 平面路径是主要额外固定成本。** `draw_quad()` 按 XZ edge 1024 RFU 细分，
   `draw_world_triangle_views()` 采样／近裁剪光照，再将无纹理平面交给
   `toy_renderer_triangle_textured_lit(NULL, ..., light=-1)`。
   `lib/graphics/renderer.c` 的 fallback 路径对通过深度测试的像素做光照插值及颜色处理。
   near / mid 30 仅恒定平面光照就分别省约 6.28 / 6.23ms；进一步关闭细分分别再省约
   2.18 / 3.58ms，但两种消融改变画面／遮挡，不能直接作为生产修复。
   near 的纹理路径命令从 10174 降至 30，mid 从 11128 降至 28；三角形总命令在
   flat-planar 消融中保持一致，说明收益并非靠减少敌人预算。
2. **太阳射线不是每帧敌人数放大项。** 正常 bake 仅在 world load/switch；Campaign
   3072 samples × 296 occluders = 909312 ray tests，约 13ms 是一次加载成本。
   每个敌人仅一次 root bilinear sample。constant-world 下敌人提交耗时仍相近，
   无证据支持敌人增加后重新烘焙光照。
3. **感染体的姿态／蒙皮／顶点／三角形提交也显著增长。** `render_infected_enemy()`
   每个实例 reset pose、更新骨骼、走模型提交；near 60 敌人提交约 10.67ms，mid 约
   13.08ms。换 legacy 身体能够明显降低这一项，但会改变外观和像素覆盖，不能当作
   正式修复结论。
4. **动漫 LOD2 不等于顶点预算已经下降。** 当前 Eula 日志为 LOD2 1030 triangles，
   仍有 20197 vertices；`prepare_gallery_vertex_cache()` 按全部 `vertex_count` 循环蒙皮
   与变换。因而只删 indices 的 LOD 仍保留高顶点 CPU 成本。不能把全部 AI actor
   的约 6ms 都归给动漫角色，RFCHAR、装备与武器同样包含在该阶段。
5. **存在实时统计失效。** Game render 迁移后，外层将整个 `rf_game_render()` 计入 overlay，
   scene/enemies/raster 及其明细未记录。本改动恢复 Game render 内阶段计时；外层继续
   记录 begin/present。scene/enemies/raster/overlay 墙钟互不重叠；首个 world flush 的
   命令、像素／路径统计独立记录，其余 flush 的墙钟和像素归 overlay。
6. **`world_light_sample()` 的直接 runtime 成本仍未定量。** `constant-world` 是整帧级消融，
   同时改变多个 helper/root 查询并受到调度和遮挡波动影响；当前数据不能支持“稳定约 2ms”
   的结论。planar fast path 完成后，应独立记录 calls/frame、total_us/frame 与 ns/call。

综合：30 FPS 的预算为 33.3ms，22 FPS 约为 45.5ms。固定场景 near 30 渲染已占约
30.1ms，mid 超过预算；再叠加多个逻辑步、战斗 effects、交互及平台呈现，能够解释为何
正常场景尚可 30 FPS，波次开始后明显掉帧。尚未精确复现用户的 22 FPS，也未确认其中
透明死亡碎片／特感／网络／present 的份额。

## 冻结结论与后续优先级

- **P0 / 下一阶段唯一推荐入口：V2 Planar Raster Optimization。** 给无纹理 V2 平面建立
  等效且更轻的 interpolated raster path，并保留颜色、雾、深度舍入及逐像素视觉结果；
  不直接关闭 V2，不回退 Static World Lighting V2 视觉规格。
- **P1：true vertex-reduced character LOD。** LOD 资产及 runtime vertex cache 必须真正缩减
  CPU skinning/vertex transform 的顶点数，不能只减少 triangle/index。
- **重新测量项：30/60 enemies scaling。** P0 完成后用同一固定场景、参数、地图与角色预算
  重跑 near/mid 的 30/60 敌人，比较阶段及整帧 scaling，不为数据好看改 benchmark。
- **重要待测：`world_light_sample()` direct runtime cost。** P0 后单独统计 calls/frame、
  total_us/frame、ns/call；在此之前不宣称它稳定占约 2ms。

细分预计算／早裁剪、SIMD、skinning 优化与 GPU backend 均不属于本 checkpoint，也不是下一阶段
入口；应在 P0 的等效 fast path 与复测完成后重新排序。

本次只添加诊断和修复性能统计，没有把画面消融应用于正常 gameplay。
真实窗口启动在当前环境失败（Wayland connect），实时统计与用户窗口 FPS 尚未现场签收；
Windows runtime 亦未覆盖。构建／逻辑验证结果以最终命令日志为准。


## 最终验证

Linux `make -j12 app-rasterfall`、最终 `--render-performance 8 --textures`、
`--logic-test`、Windows `make win-rasterfall` 与 `git diff --check` 全部通过。
Linux 构建没有新增警告；Windows 输出既有 map/console 的 strncpy 警告，未修改这些文件。
最终详细复测保持相同几何命令，near/mid 30 渲染约 29.915/35.377ms；
60 约 37.969/43.890ms。三轮表格包含系统调度波动，不能把小差异视为稳定收益。
窗口启动命令 `timeout 10s build/rasterfall --frames 2 --auto --textures` 在创建 Core 时
失败（`wayland-0` connect），因此恢复的窗口阶段统计仅完成源码／构建核查，未做实时输出验收。
