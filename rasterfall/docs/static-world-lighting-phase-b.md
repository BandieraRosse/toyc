# Static World Lighting V2 — Phase B

> 文档更新：2026-09-14
> 源码核对基线：Phase A 提交 `54a6470` 后的 Phase B 工作区；核对 world-light、Runtime Map、正常 ground/architecture submission 与 environment capture。

## 状态与入口

`rasterfall_world_light.h/.c` 继续拥有 bake/cache/sample/compose，render context 持有缓存。
`rasterfall_render_bake_lightmap()` 在 world load/switch 后先生成隔离的 Phase A/V1 cache，
再把 `session.map_ops.runtime` 传给 `rasterfall_world_light_bake_v2()`。后者的 bounds 直接来自
Runtime world，不引用 actor、material、游戏状态或可见 mesh。运行时不做太阳求交。

V2 是固定 **64×48**、包含两端 bounds 的 XZ sample grid。每格保存三个整数 Q8 分量，
另保存 bake 使用的 ground-relative `sample_y`；缓存记录 `occluder_count` 与 `ray_tests`。
V1 的 32×24 unsigned-short environment 数组单独保留，只服务延期消费者，不进入 V2。

## 分量与组合

- `environment_q8 = 256`：均匀基础环境照明；旧 east/west gradient、900 RFU BOX proximity
  和固定东侧增亮全部移出 V2，没有临时保留或重解释为点光源。
- `sun_visibility_q8 = 0 或 256`：bake origin 沿现有主光方向是否被任一静态实心 collision AABB
  遮挡；bilinear 查询可返回中间值。
- `contact_q8 = 248..256`：距实体 XZ AABB 的 Chebyshev 距离 320 RFU 内，且实体根部接近
  sample 地面高度时，最多减 8 Q8；多个实体取最弱可见度，不累加，架空梁不参与 contact。

`rasterfall_world_light_v2_q8()` 使用：

```text
environment × (192 + sun_visibility × 64 / 256) / 256 × contact / 256
```

遮挡只削弱 25% 的太阳份额；环境的 75% 始终保留。开放地面为 256，完全背阴为 192，
最强 contact 与背阴一起约为 186。随后沿用现有面调色、form lighting、material policy 和 fog。
V1 `rasterfall_world_light_q8()` 的旧乘法只在原 V1 消费链路使用。

## 三维遮挡与高度

Runtime collision 实际接口为 `bounds.min/max_x/z`、`base_y`、`height`；生成组件的
`base_y`/`height` 分别来自 component transform 后的 min/max Y，均为地面相对 RFU。
稳定 `id`/`owner_id` 仍由 Runtime Map 管理，lighting 不创建或重排碰撞。
参与遮挡的是 `collision=true`、`shape=box`、`height>base_y` 的 collision，排除既有
`air_gate` utility role；隐藏但实心的墙体仍遮挡。flat、ramp 等非 box 不作为体积遮挡器，
没有把 thin platform 或 ramp 扩成错误的整块 AABB；只有正式实心 box/component 参与求交。

射线使用 canonical surface-to-sun Q15 `(-13377,26755,-13377)`，与 form lighting 共用常量；
三轴 slab 求交，正向 t 区间相交才遮挡。double 仅用于加载 bake，不需要 libm/宿主 libc。
高位梁、门洞上梁只有射线实际经过其 bottom/top 高度才遮挡，XZ 重叠本身不足以遮挡。

每个 XZ sample 查询 Runtime surface：ground/floor、普通 platform 和 ramp 中取最高高度；
ramp 按 `axis` 对 `height→height2` 线性求值。没有 surface 时回到主地面相对高度 0。
`platform_roof` 和既有 `platform_air_gate*` utility 顶面不替代其下方主地面。
origin 为该 surface 高度 **+32 RFU**，避免同层接触面自遮挡；在 renderer 坐标中等价于
`-900 + surface_y + 32`。这不是角色眼高或模型中心采样。

限制：一层 XZ field 不能分别表示同一 XZ 下的屋顶和屋内地面，查询传入 Y 不选择新层。
普通平台/坡道覆盖主地面；其边缘的高度变化也会被低频 grid 插值。非 box 的斜坡实体和
collision=false 的纯可见平面不投射阴影。不新建 3D volume，也不修改 surface/collision 语义。

## 查询和消费范围

`rasterfall_world_light_at()` 先把 X/Z clamp 到 bounds，再映射到 `(W-1)/(H-1)` sample intervals，
保留 Q8 小数；对相邻四格三个分量分别以 64-bit 权重 bilerp，最终加半单位舍入。
最大边缘复用最后 sample，越界沿边缘延伸，退化 bounds 回到首 sample，无越界访问。

正常 ground 与 authored floor paint 继续使用一个分区平面，消费 V2，保留既有无雾策略。
map wall/texture/box、ramp/platform 消费 V2；正常 static-prop 遍历只为 boundary wall 的
独立 procedural visual-box submission 开启/关闭 V2 scope，独立诊断不主动开启。它不属于 RMESH 接入。
V2 flat quads 按 XZ edge 最大 1024 RFU 细分，sample 顶点光照并在 near clipping 中插值，
复用既有 textured rasterizer 的 opaque flat fallback 和顶点亮度路径；不修改公共 rasterizer。
textured/alpha helper 的 world 因子也消费 bilinear field，保留其原 form/fog 算法。

static RMESH（包括 `env_arch_*` 建筑网格）仍为 256/无雾的 gallery bypass；其 component
collision 可以投射 V2 光场，但网格本身尚未消费。角色/感染体/武器/VFX 和 model displays
的既有 field 查询仍走 V1 nearest cache；gallery/model/character diagnostics 原策略不扩大。
因此建筑外壳和其周围地面的受光一致性仍需后续 Phase C 审查。

## 验证入口

使用 `make app-rasterfall`、`build/rasterfall --logic-test`、`make test-map-parser test-map-runtime
test-map-components` 和 `make win-rasterfall`。world-light logic 覆盖四角、中点、三个分量独立
插值、边缘/越界、ambient floor、三维梁相交/错开/升高 origin，以及正式 Runtime surface 高度。

只读基线是根 `campaign-environment-review/`；新十二视角 BMP/PNG 与 sheet 放在
`tmp/static-world-lighting-v2-phase-b/`，命令仍为 `--environment-capture ... --textures` 与
`tools/environment_sheet.py`。退出码、bake 日志、视觉结论和性能数据以本轮生成物与审查报告为准，
不把阶段性测试数量写入稳定导航。Phase B 不自动进入 Phase C，也不自动提交。
