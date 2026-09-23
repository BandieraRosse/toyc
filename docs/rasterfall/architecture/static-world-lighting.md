# Static World Lighting V2 架构

> 状态：当前
> 所有者：Static World Lighting
> 最近核对：2026-09-21

V2 是正常 runtime 的唯一世界光照源；V1 仅供显式诊断和回归使用。可执行验证见[指南](../guides/static-world-lighting.md)，冻结现场见[历史记录](../archive/static-world-lighting-v2-freeze.md)。

## 所有权与生命周期

`include/rasterfall_world_light.h` 与 `src/rasterfall_world_light.c` 拥有 field、bake、sample 和 compose。
Runtime Map 只提供 world bounds、surface 高度与静态 collision；lighting 不创建、重排或修改地图碰撞。

normal render context 只持有 V2 cache。world load/switch 时 bake，正常帧只查询缓存，不做太阳求交。
缓存保存 world bounds 快照，因此查询不依赖后续 session 状态。Game actor、网络快照、RMESH/RFCHAR
资产均不保存光照状态。

V1 32×24 cache 属于独立 diagnostic owner，只能在显式诊断 scope 中按需 bake。正常启动不 bake V1，
默认 sampler 不会回退到 V1。

## 冻结参数

| 项目 | 当前值 |
| --- | --- |
| field | 64×48，包含 world bounds 两端的 sample |
| environment | 256 Q8，均匀环境光 |
| ambient / sun | 192 / 64 Q8，即 75% / 25% |
| contact | visibility 248..256，最大减 8 Q8，不叠加 |
| contact radius | 320 RFU |
| ground / architecture subdivision | XZ edge 最大 1024 RFU |
| normal source | V2 only |
| diagnostic source | 独立 V1 或显式 fixed lighting |

512 RFU = 1 m。field 用于低频静态世界光照，不是 shadow map 或三维 probe volume；
实际 sample 间隔随 Runtime Map 的 world bounds 变化。

## Bake 与查询

太阳使用 canonical surface-to-sun Q15 `(-13377,26755,-13377)`。bake 对 Runtime Map 中
`collision=true`、box shape、具有有效垂直范围的静态实心 collision 做三维 slab/AABB 求交；
`air_gate` utility、非 box ramp/flat 和 `collision=false` 的纯可见构件不投射体积阴影。
XZ 重叠本身不足以遮挡，高位梁只有在射线确实穿过其垂直范围时才遮挡。

每个 sample 使用 ground/floor、普通 platform 与 ramp 中的最高有效 surface 高度，再增加 32 RFU
作为射线起点以避免同层自遮挡。utility roof/air-gate 顶面不替代主地面。当前 field 是单层 XZ field；
查询 Y 不选择另一层，因此同一 XZ 下的屋顶与室内地面不能同时表达。

查询将 X/Z clamp 到 world bounds，对相邻四格的 environment、sun visibility、contact 三个 Q8 分量
分别做整数 bilinear interpolation。多个 contact occluder 取最强影响而不累加，架空构件不参与 contact。

```text
world = environment × (192 + sun_visibility × 64 / 256) / 256 × contact / 256
world light × form lighting × material policy → final color → fog
```

开放区 world=256，完整背阴=192，叠加最强 contact 约为 186。材质下限、form lighting、fog、
viewmodel 192..256 可读性 clamp、无雾 floor policy、emissive 与专用 VFX 语义保持各自原有所有权。

## Normal consumer

- ground、floor paint、primitive architecture、boundary wall、ramp 和 platform 使用 V2 顶点采样；大平面按冻结的 1024 RFU 上限细分。
- static RMESH 每实例按世界原点采样一次，通过既有 scene override 与 form/material lighting 组合。
- local/remote player、AI、RFCHAR、感染体和程序化 fallback 每个 actor/enemy root 采样一次。
- owner 世界武器和 rigid equipment 继承 owner 的 scene factor；拾取武器、投掷物按自己的世界位置采样。
- 本地第三人称与 viewmodel 共用 local player sample，保留 viewmodel 可读性策略。
- sign、interactable 与普通 auxiliary geometry 默认使用 V2。

`active_scene_light_override_q8` 是唯一的 scene factor override。`active_world_light_v2` 只控制 planar
细分和顶点提交 scope，不用于选择 V1/V2 sampler。

## 诊断例外

以下入口可以使用显式 fixed lighting 或独立 V1 scope，但不能代表正常 gameplay 的光照源：

- model gallery、isolated model tests、actor benchmark；
- Character Acceptance、lighting-props、procedural humanoid、architecture/campus fixture；
- enemy acceptance 与 silhouette/distance fixture；
- Campaign MODEL_DISPLAY、Eula、humanoid debug、Character Test Strip；
- 固定输入的 V1/V2 source regression。

Character world capture 的正常环境使用 V2，测试带可保留固定诊断光照。VFX、blob shadow、muzzle flash
拥有自己的效果语义，不形成第二份 world-light truth。

## 明确不包含

V2 不实现 layered/multi-floor light field、3D probe volume、实时 shadow map、动态点光/聚光、枪口或爆炸
世界照明、GI、SSAO、PBR、normal map、昼夜变化、地图光照语法、Face SDF、toon shader 或 anime outline。
这些能力必须作为后续版本单独设计，不能通过末端材质偏移或隐式 sampler fallback 混入 V2。
