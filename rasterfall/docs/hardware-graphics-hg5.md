# Hardware Graphics HG-5：Character geometry / GPU skinning

> 文档更新：2026-09-21
> 源码核对基线：2026-09-21 当前工作区；HG-5A 已签收。普通不透明 CPU-skinned body 已接入帧内动态 Draw，CPU animation/IK/skinning authority 不变；帧副本、Windows resize/lifecycle、四组 CPU/native 同状态视觉审阅、device-local 角色 VB 精确差分与受控 30/60 敌人性能复测均通过。

## 当前阶段

HG-5A 先保留 CPU animation、IK 与 skinning authority，上传每实例已变形顶点，让 hardware Draw 接管
角色的后续 geometry 链路；HG-5B 再独立迁移 skinning。两步必须可分别回滚，socket、weapon mount 与
finalized pose 始终消费现有 CPU-owned presentation truth。

在选择 HG-5A 的动态顶点合同和 HG-5B 的 palette 合同前，先固定 30/60 敌人正常 GPU 帧事实。
`tools/hardware_graphics_character_baseline.ps1` 从 Windows package 目录串行运行 near 30 与 near 60：

```powershell
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_character_baseline.ps1
```

脚本要求 120 个逐帧审计、丢弃前 16 帧统计，强制所有帧为 `gpu-native`，并拒绝 fallback、普通 readback
与 CPU framebuffer copy。每组保留独立 runtime log 和 `hardware_graphics_metrics.ps1` 生成的 JSON，
最终 manifest 记录 exe hash、参数与证据文件。生成物写入 `tmp/`，不提交。

2026-09-21 Intel 实机结果保存在 `tmp/hg5-character-baseline-current-b/`：30 与 60 敌人均为
120/120 `gpu-native`，零 fallback/readback/CPU framebuffer copy；第 17--120 帧 whole-loop 中位数/P95
分别为 27.209/44.686 ms 与 58.888/80.030 ms，enemy submission 为 2.628/9.470 ms 与
3.837/25.344 ms，GPU Raster timestamp 为 16.974/38.593 ms 与 26.547/63.282 ms，graphics Draw
timestamp 中位数为 2.969 ms 与 4.048 ms。单次实机数据不作为跨设备性能承诺，但 60 敌人的主要增量
同时落在 CPU 角色提交与 GPU Raster，故保持计划顺序：先以 HG-5A 移除角色 legacy geometry/RasterCmd，
再以 HG-5B 单独迁移 skinning。

## 后续实现门槛

- HG-5A：角色 body 的 CPU-skinned vertex/index/material 形成每实例动态 Draw；先覆盖普通 opaque body，
  gear/weapon 与高级材质按明确拒绝原因保留 RasterCmd。记录动态 vertex upload、实例、submesh、源三角形
  和 unexpected lowering。
- HG-5B：上传 finalized bone palette，在 GPU 生成与 CPU skinning 对照的 position/normal；animation、IK、
  socket 和 weapon placement authority 不迁移。双实例隔离、LOD、步态/瞄准/后坐与死亡表现必须保持。
- Windows 验收同时覆盖角色 acceptance、真实 world 多角色场景、30/60 敌人 strict 基线、resize、资源
  生命周期与 CPU/GPU 顶点法线差分。Linux/其他 GPU 单独报告覆盖情况。

## HG-5A 当前纵切

`rasterfall_draw.h` 定义与 graphics vertex ABI 对齐的 CPU-skinned 动态角点；producer 在现有
`prepare_gallery_vertex_cache()` 完成 CPU skinning 后，仅把普通不透明、无纹理、无 sphere/toon、无
edge、无 material ambient/specular 的 body primitive 提交为动态 Draw。其余 primitive、刚性 gear、武器
和 infected 特殊变换继续走 RasterCmd，不能因部分迁移改变 pose/socket truth。

`rf_core_mixed_frame` 拥有本帧动态角点副本，避免后续 actor 覆盖 worker-local vertex cache。GPU executor
按双帧槽各创建一个合并动态 vertex/index resource，各 body Draw 只引用自己的 index 区间；槽 recycle 后
释放。动态 backing 不注册成 world-generation 静态资源，也不参与 registry pin。`--frame-audit` 新增
`character-draw`，报告实例、Draw、三角形、上传角点和 legacy item。

Windows Intel 初步 strict-native smoke（near/0，20 帧）为 20/20 `gpu-native`，零 fallback/readback/CPU
framebuffer copy；每帧 4 个 body instance、20 个动态 Draw、6800 三角形、20400 个上传角点。该结果只证明
纵切和帧槽生命周期成立，不替代 HG-5A 完整签收；首次逐 Draw resource 原型在第一个动态 Draw 的绑定门槛
暴露问题后，已改为帧槽合并 upload。

随后补充的 mixed-frame 逻辑回归以两段动态 Draw 验证 CPU-skinned position、UV 和三组 source normal 的
逐字节帧副本、producer 缓冲覆写隔离、Draw 顶点区间以及 reset 后容量复用。Windows Intel resize 门禁在
140 帧内覆盖 `1280x720`、`1284x741`、`964x581`、`804x601` 四种 extent；所有帧保持 4 instance、
20 Draw、6800 triangles、20400 upload vertices、零 legacy body item，稳态 GPU upload 为 1387204 bytes，
同时保持 strict-native、零 fallback/readback/CPU framebuffer copy。`tools/hardware_graphics_resize.ps1` 已据此
取消“稳态零上传”的 HG-4 假设，改为要求 HG-5A 动态上传在 resize 全程稳定。

`tools/hardware_graphics_character_baseline.ps1` 现要求每帧存在合法 `character-draw` 审计，并已完成新一轮
30/60 敌人 120 帧 correctness 采集；两组均为 120/120 `gpu-native` 且无 fallback/readback/copy。该轮机器
负载下 60 敌人 whole-loop/GPU Raster 中位数为 151.646/70.345 ms，明显偏离前置 58.888/26.547 ms，
暂不作为 HG-5A 性能结论，需在受控冷启动条件重跑。当时角色视觉对照与 device-local 数值差分尚未签收；
两项现均已由下述固定 tick capture 和 vertex diff 门禁补齐。

角色视觉对照由 `tools/hardware_graphics_character_capture.ps1` 串行采集 near/mid × 0/30 敌人四组固定场景，
每组保存 CPU PPM/BMP、strict-native BMP、stdout/stderr、GPU runtime log 和逐像素差异指标。脚本要求所有
GPU 帧为 native、零 fallback/readback/CPU framebuffer copy，并要求 `character-draw` 在整轮保持稳定、
上传角点数等于源三角形数乘三。脚本记录 `legacy_items`，因为程序化角色与不支持材质仍明确留在纵切外；
只有 near/0 resize 场景要求其为零。CPU 与 GPU 使用不同光栅器，像素指标只作为审阅证据，不强制为零；
必须人工并排检查四组 BMP 后才能把视觉 checkpoint 标为 PASS。采集专用的
`--gpu-normal-fixed-tick` 令每个渲染帧恰好推进一个 16ms gameplay tick，避免 CPU/native 耗时差异令第 30 帧
落在不同 simulation state；性能基线不使用该参数，继续按真实墙钟驱动。

2026-09-21 Windows Intel 正式采集保存在 `tmp/hg5-character-capture-fixed-20260921/`。四组 CPU 与 GPU
日志均为 30/30 帧且每帧 `ticks=1`；strict-native 保持零 fallback/readback/CPU framebuffer copy。人工并排
审阅确认 near/mid 的角色队形、姿态、遮挡、武器/装备衔接及 30 敌人特殊角色位置一致，未见迁移 body
缺面、拉伸、法线翻转或 GPU 专属轮廓错误。大面积像素差主要来自既有 sky/world 光栅差异，不作为角色
geometry 失败。HG-5A 视觉 checkpoint 通过；数值 checkpoint 由下述 device-local VB 差分补齐。

`--gpu-character-vertex-diff` 在 normal-scene 第 30 帧执行一次诊断：动态角色顶点完成 staging 到
device-local vertex buffer 后，通过 GPU transfer copy 回读实际绑定给 indexed Draw 的缓冲内容，逐个 `int32`
对照本帧 CPU-skinned reference 的 position、UV 和三组 source normal。该门禁验证 HG-5A 上传结果及
`VK_FORMAT_R32*` vertex-input 字节解释边界，不把它描述为 HG-5B 的 GPU skinning 或 shader 输出验证。
`tools/hardware_graphics_character_vertex_diff.ps1` 负责串行运行、严格 native/fixed-tick 检查和 manifest 输出。
2026-09-21 Windows Intel near/0 第 30 帧共比较 20400 顶点，position/normal/UV mismatch 与最大差值均为 0。
因此 HG-5A 数值 checkpoint 通过。

最终性能复测发现，最初每帧通过同步 staging submit 创建动态资源会把上一帧 GPU 队列等待计入
`mixed_preflight`。资源上传现优先选择同时满足 device-local 与 host-visible 的内存直接写入；没有该内存
类型的设备仍使用单次 staging submit fallback。该调整不改变 vertex/index usage、descriptor、双帧槽生命周期
或 device-local 合同。Windows Intel 最终证据位于
`tmp/hg5-character-baseline-host-visible-20260921/` 与
`tmp/hg5-character-baseline-final-20260921/`：两轮 near 30/60 均为 120/120 strict native，零
fallback/readback/CPU framebuffer copy。丢弃前 16 帧后，两轮 30 敌人 whole-loop 中位数为
32.706/30.278 ms；60 敌人为 53.647/60.560 ms，GPU Raster 为 26.899/30.559 ms，GPU Draw 均约
1.823 ms。相较纵切前 60 敌人 58.888/26.547/4.048 ms 的 whole-loop/Raster/Draw，中位数随本机负载
小幅摆动，但角色 Draw 明显下降，未见 HG-5A 引入的持续退化；P95 不作为跨设备承诺。当前最终 SHA
对应的 vertex diff 再次精确比较 20400 顶点且全部差值为零，证据位于
`tmp/hg5-character-vertex-diff-final-current-20260921/`。HG-5A 正式签收，下一步进入 HG-5B GPU skinning。
