# Static World Lighting V2 — FROZEN

> 文档更新：2026-09-14
> 源码核对基线：`139ead1bcdc3e11e41a5820f367cabf5bdac6781`（Phase C3）及 Phase D 工作区；核对 world-light owner、consumer、CLI、Linux/Windows 构建和固定输入 capture。未自动提交。

## 冻结决定与 baseline

Phase D 完成 Final Convergence / Freeze。Static World Lighting V2 — **FROZEN**。
这是当前正式 Campaign 的静态单层世界光照边界；不扩展新能力，不开始 Anime Rendering V1。

| 项目 | Phase D baseline / 最终值 |
| --- | --- |
| field | 64×48，3072 个包含两端的 sample |
| environment | 256 Q8，均匀，无旧 east/west gradient/proximity boost |
| ambient / sun | 192 / 64 Q8，75% / 25% |
| contact strength | 最多减 8 Q8，visibility 248..256，不叠加 |
| contact radius | 320 RFU = 0.625 m |
| ground / architecture subdivision | XZ edge 上限 1024 RFU = 2 m；与 field resolution 独立 |
| normal runtime ownership | render context 只持有 V2，加载/切换 bake V2 |
| diagnostic ownership | 独立 V1 32×24 cache，显式 scope 按需 bake |

ambient/sun/contact、field resolution、subdivision 均未调整。

## 世界空间尺度与 resolution 决策

`build/map-inspect rasterfall/assets/maps/rasterfall.map` 的 Runtime world 输出：

```text
X bounds = -45200 .. 33200 RFU; span = 78400 RFU = 153.125 m
Z bounds = -33200 .. 33200 RFU; span = 66400 RFU = 129.6875 m
world X/Z aspect = 1.180723
64/48 field aspect = 1.333333
```

512 RFU = 1 m。按 Phase D 要求的 span / resolution 口径：

| 方向 | 每 cell RFU | 每 cell m |
| --- | ---: | ---: |
| X：78400 / 64 | 1225 | 2.392578 |
| Z：66400 / 48 | 1383.333333 | 2.701823 |

Z/X = 1.129252，Z 尺寸约大 12.93%，接近方形且适合当前低频世界光照。
实现是包含 bounds 两端的 sample grid，真正 bilinear sample 间隔分别为
78400/63 = 1244.444444 RFU（2.430556 m）、66400/47 = 1412.765957 RFU（2.759308 m），
Z/X = 1.135258。不要把 sample 数误写成 interval 数。

64×48 已有合理空间尺度依据，完整画面未见要求升分辨率的缺陷，故没有触发 64×64 A/B。
64×64 的 span/resolution 尺度会成为 1225×1037.5 RFU，sample 数增加 33.33%；
这只是尺度/容量推算，没有候选 bake 或视觉性能实测。最终保留 64×48。

## 最终光照契约

`include/rasterfall_world_light.h` / `src/rasterfall_world_light.c` 拥有 field、bake、sample、compose。
coverage 是 Runtime world bounds；cache 快照让查询不依赖后续 session 状态。
world load/switch 时 bake，正常帧不做太阳求交。当前 Campaign 有 296 个参与遮挡的 collision，
bake 执行 909312 次 ray tests。

太阳使用 canonical surface-to-sun Q15 `(-13377,26755,-13377)`，对 Runtime Map 的实心
box/component collision 做三维 slab/AABB 求交，排除 air_gate utility。
纯 XZ 重叠不足以遮挡；flat/ramp 非 box 和 collision=false 可见构件不投射体积阴影。
每个 sample 采用 ground/floor/普通 platform/ramp 的最高有效 surface 高度 +32 RFU；
utility roof/air_gate 顶面不替代主地面。query Y 不选择光照层。

sun visibility bake 为 0/256；查询时三个 Q8 分量独立、整数 bilinear、舍入并 clamp bounds。
contact 在接近地面的实体 XZ AABB 外 Chebyshev 距离 320 RFU 内减弱，多个实体取最强
contact 而不叠加，架空梁不参与。

```text
world = environment × (192 + sun_visibility × 64 / 256) / 256 × contact / 256
world light × form lighting × material policy → final color → fog
```

material policy 实现为 scene×form 后应用材质下限/既有调色策略，不能把它理解成另一个无条件
Q8 乘数。开放区 world=256，完整背阴=192，叠加最强 contact 约186。
FACE/SKIN/EYES/HAIR 原下限 224/224/240/176 保留；viewmodel scene clamp 192..256 保留。
原 floor submission 的无雾策略、material emissive 与专用 VFX 语义保留。

V2 是 sole normal-runtime world-light source：ground/floor、primitive architecture、boundary wall、
static RMESH、local/remote players、AI/RFCHAR/感染体、owner 世界武器/装备、拾取武器、投掷物、
viewmodel、sign/interactable/auxiliary geometry 都从 V2 或 owner 的单一 scene factor 消费。
static RMESH 每实例原点一次 sample；actor/enemy 每 root 一次，武器继承 owner，viewmodel 共用本地值。

V1 = diagnostic/test only。唯一 `active_scene_light_override_q8` 表示 scene factor；
`active_world_light_v2` 仅控制 planar vertex/subdivision submission，不选择默认 sampler 版本。
fixed diagnostic lighting 不与 normal floor policy 混用。
具体固定 gallery/Character Acceptance/lighting-props、enemy diagnostic V1、Campaign renderer-only
测试带与 C2 baseline 例外见 [Phase C3](static-world-lighting-phase-c3.md)。
它们不能用来代表正常 gameplay 的光照源。Character Acceptance 的 neutral RF Humanoid carrier
并不声明全部 FACE/EYES 材质 role；四类 policy 的有效性同时由实际材质命令 logic 回归验证。

## 验收证据与性能

本轮原始日志、BMP、PNG、SHA256 manifest 和性能 JSON 位于
`tmp/static-world-lighting-v2-phase-d/`，均为本地生成物，不提交。
`final/world/sheet.png` 为最终十二视角；另保留 C2 open/shadow/reopen 和六类 enemy shadow。

视觉结论：north/spawn 开放区保持可读；west/east/north facility 与 power-yard 的地面、建筑
RMESH 和设施阴影关系一致，open→shadow→open 的固定动态场景没有某类对象异常增亮。
背阴角色/感染体的 body 与持有武器可辨，viewmodel 明暗响应但可读。
高位梁、门洞及架空结构未见由 XZ-only projection 引起的大面积错误遮挡；三维梁高度回归通过。
ground/ramp/platform 未见不可接受的规则 grid banding；细分/插值边界在当前十二视角足够弱。
baseline 与 final 世界 BMP 字节一致，视觉 A/B 结论是保留 baseline；没有做参数 A/B。

性能为 1280×720 fixed-input headless：每场景 warm-up 一帧后计五帧，串行三轮取中位数；
不含 BMP IO、present、pacing。只比较日志中的 `mode=V2`；旧 `mode=baseline` 是 C2 诊断 V1
消融，不能误称 Phase D baseline。两组串行复测使用同一份修复后 binary；因为光照参数/算法未变，比较的是冻结 baseline 的重复运行稳定性，
不是不同实现的性能 A/B。最早 baseline capture 与其他构建/capture 并发，其计时未用于此表。

| 场景 | frame ms baseline→final | equivalent FPS | raster ms | actor/enemy ms | commands | dynamic queries |
| --- | --- | --- | --- | --- | ---: | ---: |
| open | 23.944→24.038 | 41.76→41.60 | 13.040→12.904 | 2.342→2.331 | 23485 | 9 |
| shadow | 20.493→20.140 | 48.80→49.65 | 10.111→9.948 | 2.314→2.254 | 19964 | 9 |
| reopen | 23.240→24.603 | 43.03→40.65 | 12.096→13.486 | 2.398→2.530 | 23485 | 9 |

Campaign bake 中位数 13.294→12.917 ms；默认 landing 再切 Campaign 的 startup lighting
bake 合计中位数 13.822→13.461 ms（只计 bake，不声称覆盖整个进程启动）。
V2 field/cache `sizeof` 49184 bytes，render context 49232 bytes，两者未变。
queries 是已有 dynamic counter，不是全部 ground/static/auxiliary 查询总数。
frame 波动约 −1.7%..+5.9%，命令数/查询数/内存不变且算法未改，当前回退可接受；无需性能改造。
equivalent FPS 不代表真实窗口 gameplay FPS。

固定输入重复 BMP：world 21、Character Acceptance 31、enemy 145、Character world 15、
lighting-props 1，共213张，全部字节一致；十二视角包含在 world 集合中。
Character world 入口补齐已有 headless 初始化、seed=1 和 Campaign 选择；不改变其测试带固定
诊断光照，也不改变正常启动/随机 seed。该入口 baseline 是修复后第一次可执行 capture，
不是声称修复前已经完成离屏运行。

Linux GCC freestanding baseline/final build、Windows MinGW baseline/final build、logic test、
map parser/runtime/stable surface reference/component collision 回归全部通过。
world-light/source ownership、scene×form/material policy 由 logic test 覆盖。
Character/enemy/world/lighting-props runtime captures 在 Linux headless 完成；Windows 仅 build，
未做 Windows runtime。`git diff --check` 通过。

真实窗口尝试 `timeout 20s build/rasterfall --frames 120 --auto --textures`：Wayland connect 失败，
RF Core host 初始化失败。**interactive Wayland acceptance unavailable in current environment**。
连续移动、实时 flicker/grid snapping 和 gameplay FPS 未作真实窗口签收；本轮使用固定输入
headless world capture、bilinear/三维遮挡 logic 与平台构建作为证据，该环境缺项不是冻结 blocker。
不为窗口环境修改架构。

## Architecture final audit 与边界

Normal Campaign → V2 only；V1 → diagnostic/test only。
正常 render context 不含 V1 cache；正常 startup 不 bake V1；默认 sampler 只选 V2。
唯一 scene override 和 fixed diagnostic policy 保持明确；sign/interactable/auxiliary 默认 V2。
actors/weapons 不保存 world-light gameplay state，network 不携带 lighting state；
RMESH/RFCHAR 格式与资产不变。Phase D 源码只修复已有 Character world 验收输入配置。

冻结条件：开放区可读、设施过渡连贯、static RMESH 与世界一致、动态对象共用光照空间、
character policy 有效、高位结构无重大误遮挡、无不可接受 grid banding、V2-only normal / V1-only
诊断、可接受性能、确定性 capture、Linux/Windows build、文档契约一致均成立。
保留上述真实窗口/Windows runtime 覆盖限制，不宣称全平台交互测试通过。

V2 不解决：layered / multi-floor light field、3D probe volume、realtime shadow map、dynamic point
light、dynamic spot light、muzzle flash world illumination、explosion dynamic light、GI、SSAO、PBR、
normal maps、day/night、full map lighting syntax、Face SDF、toon shader、anime outline。
这些全部属于后续版本；单层 XZ、低频阴影、实例/root 单点照明与纯可见几何不投影的限制保留。

推荐 checkpoint 标题：`冻结 Static World Lighting V2 并完成 Phase D 验收`。
内容为本冻结记录、rendering/index/runtime/platform 导航收敛和 Character world capture 输入修复；
不包含生成物、私有资产，不自动提交。
