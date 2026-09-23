# GPU Scene 阶段 0：迁移接口合同

> 状态：设计合同；actor 身份和只读 snapshot 已隔离实现，其余接口尚未实现；版本草案 V1
>
> 核对日期：2026-09-23

本文定义[活动计划](gpu-scene-renderer.md)第二步的最小接口。它只约束数据和所有权，
不改变现行 mixed renderer。现行资源身份和 slot 行为见
[`rasterfall_render_resources.h`](../../../rasterfall/include/rasterfall_render_resources.h)、
[`rf_core_mixed_frame.h`](../../../rasterfall/include/rf_core_mixed_frame.h) 与
[`rf_gpu_mixed_executor.c`](../../../gpu/src/rf_gpu_mixed_executor.c)。
隔离的 actor generation tracker 与只读 actor snapshot 构建已由 `rf_gpu_scene_identity.h/.c`
实现并有独立逻辑用例。当前 snapshot 只含 camera、extent、frame/world identity 与 actor 展示值；
world/transient/UI 输入、GPU Scene extraction、资源表和正常帧接线仍未实现。

## 帧身份与状态

每个结构携带 `abi_version = 1`、`byte_size`、单调 `frame_id` 和 `world_generation`。
`frame_id` 标识本次冻结及诊断计时；`world_generation` 在 world load/unload 时增长，禁止
将前一世界的实例混入本帧。`byte_size` 用于拒绝版本或布局不匹配，不能靠读取未声明尾字段兼容。
所有数组以 `(first, count)` 索引本帧 arena；冻结后直到对应 slot 退休均只读。

状态依次为 `BUILDING → FROZEN → PREFLIGHTED → SUBMITTED → RETIRED`，失败进入 `FAILED`。
Core 只在 `FROZEN` 后解析并 pin 全部资源、验证引用和容量；任一失败在首个 target 写入前
结束整帧。`SUBMITTED` 后的失败使整帧失败并保留 pin 至 GPU 完成或明确 teardown，不能重用
slot、CPU replay 或把半帧 present。空层仍保留有序 pass 节点，避免由 producer 名称推断 target。

## PresentationSnapshot V1

由 game runtime 在固定 tick 的 presentation cache 求值后构建，renderer 只读；不包含 GPU handle、
GPU slot、CPU model 指针、`toy_game *` 或可回写的 gameplay 字段。

| 字段组 | 最小内容 | 身份与顺序规则 |
| --- | --- | --- |
| Header | `abi_version`、`byte_size`、`frame_id`、`world_generation`、固定 tick/展示时间、extent | 一帧只冻结一次；extent 与将要提交的 target 一致 |
| Camera | 主 camera 的位置、方向、pitch、投影/near；VIEWMODEL 的独立 near | 全部从同一 presentation 时刻取得 |
| World | map/content generation、地图可见状态、air gate/静态光照查询上下文的冻结值或只读版本引用 | 不复制碰撞真值为画面几何；world 版本不得在 extraction 中变化 |
| Actor | 稳定 `actor_id`、`actor_generation`、presentation transform、动作/武器/装备语义、材质展示覆盖、可见状态 | 按现行提交顺序存储；ID 复用必须增长 generation；不把数组下标当长期身份 |
| Transient | 投射物、交互物、旗帜、effect 的稳定本帧 ID、种类、变换、alpha/时间语义 | 同源内维持现行提交次序；生命周期只需覆盖本帧时可用 `frame_id + local_index` |
| UI input | HUD、name/status、viewmodel 的只读展示值 | 与同一帧 actor/world 状态一致；screen overlay 的像素不是 snapshot 字段 |

Snapshot 不持有跨帧裸指针。无法在一帧内拷贝的只读 world 数据以 `(world_generation, stable_id)`
解析，并由 Core 保证其版本覆盖 extraction；解析失败使本帧失败。演员 ID 不能仅凭
`toy_game_actor.actor_id` 或 `TOY_GAME_MAX_ACTORS` 下标推断稳定身份。
当前 `rf_gpu_scene_snapshot_build_v1` 消费完整 actor slot 输入，输出按 slot 顺序压紧的值数组；
inactive slot 也必须提交，以便身份状态识别消失和复用。重复来源身份、逆序 frame/world、无效
extent 或输入错误会拒绝整次构建，tracker 与输出保持原值。该数据模块尚不读取 `toy_game`。

源码核对：本地 AI 的 `actor_id` 通常由可重用 slot `+1` 生成；清除 hired AI 后同一 ID 可分配给
新 actor。remote player 使用 `100 + player_id`，断线后也可复用；客户端 snapshot 投影又按
`actor_index + 1` 写入 `actor_id`。因此现有 `actor_id` 只能作本帧来源标签，不能单独作跨帧身份。
V1 的 actor generation 由隔离的 snapshot tracker 按 `(source, source_id)` 的生命周期维护：同一来源身份
连续出现在相邻 snapshot 时，即使更换 slot 也保持 generation；消失后重现或 world generation 更换时
分配未使用的 generation。slot 只决定本帧输出顺序，不能作为身份计数器。
客户端须以网络 `actor_index` 的来源命名空间建立此 generation，不能把投影后的 `actor_id`
当作 host ID。generation 状态不写入 `toy_game`，也不影响网络协议。

## GPU Scene V1

scene extraction 消费一个冻结 snapshot，完成保守可见性、LOD、pose/IK、socket 和附件 placement；
结果只写入本帧 GPU Scene arena。所有 GPU 可见引用均是 handle/generation 或本帧数组索引。

| 字段组 | 最小内容 | 验证规则 |
| --- | --- | --- |
| Header | snapshot 的 `frame_id/world_generation`、版本/大小、scene generation、extent | 与输入完全一致；不得在提交时回读 gameplay |
| Resource table | mesh `slot/generation`、primitive、texture 子资源 index、材质版本或本帧覆盖 index | generation 非零；Core preflight 逐项 resolve/pin；纹理不以 pointer identity 进入 scene |
| Instance | 稳定 `instance_id/generation`、来源 actor ID/generation 或 world ID、mesh/material 引用、finalized transform、scene light Q8、pose/palette range | 相同 snapshot 生成相同身份及内容；rigid 附件引用同 actor 的 finalized placement |
| Material override | base/sphere/toon/edge/alpha、双面、lighting floor、tint/衣裤色等实际需要的覆盖字段 | 显式标志和版本；unsupported 在 preflight 报错，不能静默降级 |
| Ordered item | `layer`、`kind`、`instance_index` 或 effect/UI payload range、`submission_ordinal`、`depth_test/write`、blend/coverage | WORLD opaque 可按材质批次重排且结果不变；transparent/effects/viewmodel/overlay 保留稳定 ordinal |
| Pass list | SKY、WORLD opaque、WORLD transparent、EFFECTS、VIEWMODEL、POST 边界、OVERLAY、present 的 target 读写与依赖 | VIEWMODEL 独立 depth/coverage；没有隐式 Raster/Draw bridge |

`instance_id` 在 world generation 内稳定，动态 actor 使用 `(actor_id, actor_generation, role,
attachment_slot)` 派生，静态 map/prop 使用 authored/runtime stable ID；同一 actor 的 body、gear、weapon
角色不同，不能复用一个 ID。generation 只在身份的对象或资源生命周期更换时增长；pose 更新只改变
本帧 payload。`submission_ordinal` 是冻结时的原始可见提交序号，透明项严禁材质排序或从 ID 重建顺序。
材质/贴图覆盖不能暗含资源指针；缺少资源、范围越界、重复 ID 且 payload 冲突均拒绝整帧。

地图来源核对：Runtime Map 的 `rf_map_runtime_render.id` 是 authored stable ID；当前
`rasterfall_map.c` 在投影时按 `legacy_index` 恢复旧提交顺序，并把记录拷贝进不含 ID 的
`toy_map_draw`。V1 extraction 应从冻结的 Runtime Map 按同一投影顺序取得 `id` 与 payload，
分别填 `instance_id` 和 `submission_ordinal`；不能用 `toy_map_draw` 下标作跨帧身份，也不能按
ID 字典序重排透明项。world generation 变化后才允许同一 authored ID 代表新世界资源。

## 接口所有者与调用边界

| 接口 | 所有者 | 输入 → 输出 / 失败 |
| --- | --- | --- |
| `snapshot_build_v1` | `rf_game_runtime` 及 presentation cache | game/session 只读投影 → 冻结 snapshot；不能修改权威状态 |
| `scene_extract_v1` | `rasterfall_render` 及 character/map producer | snapshot → GPU Scene；pose/socket 在此完成，禁止 executor 遍历 game |
| `scene_freeze_preflight_v1` | Core Host | Scene → 已验证资源表、pin 集合、pass 依赖；提交前全量检查 |
| resource prepare/bind | GPU cache | `(handle, generation, primitive, texture_index)` → 不可变 device resource；prepare 可在 preflight 上传，bind 不分配 |
| frame-slot execute/retire | GPU executor | 已 preflight Scene → slot upload/skinning/draw 与完成信号；退休后释放本帧 pin/arena |
| native acquire/present | Windows presenter/backend | 同 device/queue 的最终 target → swapchain generation；present 完成与 render fence 分开跟踪 |

Core 保持旧 mixed 与新 Scene 的**整帧启动选择**，不能按 producer 或 pass 在两者间切换。
新接口落地时再为结构定义 `sizeof`/布局静态断言与独立逻辑用例；此文中的接口名是职责名，
不是已导出的 C 符号。

## 冻结前仍须决策

1. prop 与 transient ID 由各现有数据源如何产生；地图 render ID 和排序来源见上文，actor
   generation 的 adapter 规则也见上文。实现时需覆盖 slot 复用、断线重连与 world 切换。
2. normal frame 中直接像素的 world label/status、effects overlay、HUD 如何变为有序 GPU payload；
   保持[覆盖矩阵](gpu-scene-coverage.md)中的可见内容与遮挡语义。
3. RTX 3050/Intel 实际能力枚举后，确定共同 Vulkan feature baseline 与兼容实现；不能在 V1
   结构中预设 descriptor indexing、dynamic rendering 或 synchronization2 一定可用。
