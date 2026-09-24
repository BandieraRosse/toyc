# GPU 渲染架构

> 状态：当前
> 所有者：Rasterfall GPU renderer、Core Host 与 Windows presenter
> 最近核对：2026-09-23

本文只定义 GPU 渲染的稳定所有权、数据流和失败边界。验收命令见
[GPU 验收与诊断](../guides/gpu-validation.md)，设备档位和性能门槛见
[GPU 性能标准](../reference/gpu-performance-standards.md)，尚未完成的工作只见
[当前活动计划](../plans/README.md)。阶段调查、单次设备现场和已撤销实验位于
[GPU 2026-09-22 归档](../archive/gpu-2026-09-22/README.md)。

## 所有权

| 职责 | 所有者 |
| --- | --- |
| world、角色、特效 producer 与层顺序 | `rasterfall/src/rasterfall_render.c`、`rasterfall/include/rasterfall_render_frontend.h` |
| 帧冻结、资源 pin、整帧 preflight 与执行编排 | `rasterfall/src/rf_core_host.c` |
| GPU service、mixed executor 与资源 cache | `gpu/src/rf_gpu_vulkan_backend.c`、`gpu/src/rf_gpu_mixed_executor.c`、`gpu/src/rf_gpu_resource_cache.c` |
| normal runtime 与 presenter 生命周期 | `rasterfall/src/rf_game_runtime.c`、`windows/src/window_sdl.c` |
| CPU reference 与 Raster ABI | `rasterfall/src/render/rasterfall_draw_reference.inc`、`rasterfall/include/rf_gpu_raster_abi.h` |

`rasterfall.c` 只负责进程、输入、固定步长主循环和顶层编排。renderer 读取玩法或展示投影，
不修改 `toy_game` 的权威结果。地图文本、Runtime Map、玩法碰撞和可见几何保持分层。

## 混合帧数据流

```text
game/session presentation snapshot
    -> renderer producers
    -> retained frame (Draw + RasterCmd)
    -> Core freeze / pin / whole-frame preflight
    -> mixed executor on shared color/depth targets
    -> native presenter
```

producer 按 WORLD、EFFECTS、VIEWMODEL、POST、OVERLAY 的固定层序生成 retained frame。普通 opaque
static RMESH、持久 ground/map/boundary geometry 和角色 body 可进入 hardware Draw；动态、透明、特效、
viewmodel、overlay 及尚未满足数值合同的内容保留为 RasterCmd。

Core 必须在任何 target 写入前冻结执行计划并完成整帧 preflight。required 模式下，unsupported、编码失败、
资源 generation 不匹配或 presenter 失败都会使整帧失败；禁止先写入部分 GPU target 再回放 CPU 整帧。
Draw 与 RasterCmd 共用 color/depth target，命令顺序、CLEAR/LOAD、透明、viewmodel 和 overlay 语义由统一
recording 保持。

producer 身份只用于诊断，不自动形成 target 可见性边界。相邻 WORLD Draw spans 之间没有 Raster span 时，
executor 可按原顺序合并为一个 graphics batch；Raster span 是硬边界。任何进一步合并或迁移必须同时
保持画面合同并减少实际 Draw/Raster run、bridge 或 whole-loop 成本，不能只以 RasterCmd 数量下降签收。

normal AI world 对模块化角色先按 actor 顺序求值 pose/IK 并冻结全部可见 body Draw，再按相同顺序提交
opaque gear/weapon RasterCmd。附件继续读取对应 actor 的 finalized pose、placement 和 scene-light override；
该编排不得跨越 transparent、effects、viewmodel 或 overlay 层。

## Raster 与诊断合同

indexed Draw 的正面必须与正式 CPU renderer 一致：屏幕 Y 向下，CPU 接受 `(c-a) × (b-a) < 0`，
正高度 Vulkan viewport 使用 `VK_FRONT_FACE_CLOCKWISE`。普通与 integer-compatible pipeline 共用该规则；
单面材质剔除背面，双面材质保留两面，opaque 深度测试/写入仍为 GREATER_OR_EQUAL。
独立测试若使用相反的叉积顺序，必须同时反转符号判定，不能把错误 GPU 绕序写成 reference。

Raster binning 按原 command index 将命令写入 tile 列表。binned shader 可用有序索引跳过当前 segment 外
命令，但必须在 `segment.end` 停止；独立 full-scan shader 保留为 differential 对照。帧审计按 producer
记录 RasterCmd/span，并按实际 bridge 记录方向、color/depth traffic、层、相邻 producer 和 target generation。

GPU timestamp 属于完成的旧 frame slot，必须按其 frame ID 回填，不能直接归到当前 CPU frame。
`valid` 要求 requested 与 recorded 相等且 dropped 为零；退出时未回收的尾部样本不进入分位数。
graphics submit/wait 是队列关系证据，不等于某个 producer 的 GPU 时间。

正常游戏画面使用中性 fog。RasterCmd fog 字段、CPU/GPU consumer 和底层 Post Fog 测试合同仍可保留，
但 normal runtime 不把它们接入画面。

## 资源生命周期

- 模型 registry 拥有不可变 CPU bundle 与 stable handle/generation；GPU cache 拥有 device resource。
- Core 在 begin-frame pin 本帧引用；frame slot 完成前不得释放。world 切换后的旧 generation 只有 pin 清零
  后才能回收。
- 双帧 slot 分别持有 extent target、command/fence/query 和动态 Draw backing；不可变 mesh/texture cache
  由 executor/device 统一持有。
- 已完成的 frame slot 复用角色 skinning 的动态 Draw resource、顶点/索引 buffer 和 descriptor；bind/palette
  每帧更新并执行 skinning。输入超过既有容量时，在 slot recycle 后销毁旧 generation 再创建更大资源。
- raw Raster 分段重传必须保留先前录制引用的 backing，直到相关录制完成或销毁。
- resize 只重建 extent 相关 target、slot binding 与 swapchain，不得重复上传稳定 world mesh/texture。
- swapchain 由 backend 唯一拥有；acquire、render fence 与 present completion 分开跟踪。正常热路径禁止
  queue-idle，只有明确的 recreate/teardown 边界可以排空队列。

## 独立 Scene fixture 提交

`rf_gpu_scene_native.c` 拥有显式 `--gpu-scene-native-fixture` 的冻结输入、资源解析、整帧验证和单个
Scene slot；正常帧仍由 Core mixed executor 编排。fixture 只提交 `opaque_box` 地图几何、RF rifleman
body 和 HEAD 附件。地图沿用正式地图 mesh builder；catalog ID 解析为独立 registry 的 handle/generation。
材质、索引、palette 范围与变换全部检查后才统一 pin；设备准备失败也不会提交 target 写入。

CPU pose 仍由独立 instance 求值，并冻结 finalized pose 的 bind-normal 策略。body palette 上传到既有 compute skinning，rigid HEAD 的 finalized
矩阵通过单骨 palette 消费，资源 `position_scale` 到 RFU 的换算只在资源解析侧执行一次。不可变地图
资源跨帧保留，动态 device backing 在退休后 update 或增长；CPU pose 每帧独立求值，pack/upload backing 在
slot 退休后按容量复用，world 资源失效不回收 CPU backing。

graphics owner 持有独立 command pool/buffer、fence、acquire semaphore 和 WORLD color/depth。
`rf_gpu_graphics_scene_present` 将全部 mesh 放入同一 render pass，直接 blit color 到 backend 唯一
swapchain，完全不接收 Raster stream，也不调用 bridge。render-finished semaphore 按 swapchain image
索引持有，重新 acquire 同一 image 才证明此前 present wait 完成。单 graphics queue 串联上传、compute、draw
和 present；当前 fixture 在每次提交后显式等 fence，再释放 pin，不是多帧流水实现。
Scene 的四个 GPU timestamp 查询随 command buffer 重置，围住 WORLD draw 与 swapchain blit；fence 退休后按
提交时的 frame ID 读取。设备不支持 timestamp 时报告 unsupported；这些值不覆盖此前独立提交的上传和蒙皮。

`scene_retire` 成功才允许 slot 复用；失败保留 pin 到 graphics/backend teardown 排空 GPU 后。resize
只替换 extent target/swapchain，world 失效后的旧 handle 在 fence 完成前保持可解析。诊断 capture 直接
读取 Scene color/depth，不经过 Raster 转换；该显式 readback 与连续 native 提交分别统计。复现和未完成
门禁见 [Scene fixture 指南](../guides/gpu-scene-fixture.md)。

正常帧的 `--frame-audit` 另从 Runtime Map 冻结 V2 world 元数据及独立 V1 地图渲染值帧，
并用后者提取 wall、box、ramp、style 2 platform 四类不透明网格。地图范围、出生区及
authored ground 策略另按值冻结，分区地面与 mixed 共用同一网格生成算法，作为第五类资源。
静态 object 按 authored ID 和 `level->props` 投影顺序冻结；现行持久 boundary wall 几何从
此值帧生成第六类资源。可见 static RMESH 按同一冻结 object 值、V2 实例光照、资产 profile
与既有 Draw 材质规则提交；镜头外实例沿用 mixed 的模型 AABB 可见性检查。资产按模型路径进入同一
Scene registry，跨实例共享 GPU cache，已 pin 的旧代随帧退休。当前仅在正常帧审计离屏提交。
普通 `TOY_MAP_DRAW_MODEL` 盒体从冻结 render 值生成第七类网格；floor 值帧另冻结现行该入口
使用的 V1 诊断光照场，其面片保持旧路径的单四边形划分和每三角形中心采样。
SIGN 的牌柱、牌面及双面 bitmap 字形从同一 render 值生成第八类网格；字形保持旧路径
的横向连续像素 run 合并，牌体与文字均按三角形中心采样 V2 光照。
展示模型 style 1、2、6、9、12 的旧方块人/圆柱人形体共用第九类网格，按三角形中心
采样冻结 V1 光照。style 3–5 的特殊感染体从旧 rig 目录求静态姿态、世界变换及面光照，
生成第十类网格并逐三角形采样冻结 V1 光照；style 7–8、10–11、13–14 的六种导入感染体展示模型复用旧 idle rig 姿态、CPU skinning 和材质，生成第十一类网格并烘焙逐三角形冻结 V1 光照。
生成网格的离屏 Scene draw 使用读取顶点光照的 graphics 管线；静态 RMESH 保留
整数深度兼容管线。片元顶点光照上限为 384，与旧 Raster 的范围一致。
独立 Scene world registry 持有 generation handle，按 world/map、V2 light generation、
冻结的完整绘制值、地面与 object 输入复用网格，
并延迟释放已 pin 的旧代。独立 native fixture 在三件套
结束后调用可复用的 `rf_gpu_scene_world_gpu_prepare`，将 fixture 中非空的六类网格经 GPU cache
预备并提交离屏 Scene WORLD，检查 color/depth 覆盖、
cache hit 和旧代退休。正常帧 `--frame-audit` 在 mixed present 完成后，使用同一 Vulkan context
创建独立 graphics owner 和 GPU cache，消费当帧冻结的 map/prop handle 与 render camera，将非空网格
提交到独立 Scene target 并读回 color/depth。八名非 hired 正式模块化队员各自的冻结 body palette、
被动装备、AK 武器与 V2 actor 光照值也经逐 actor 独立 GPU 资源预备，和地图 draw 共用该 Scene WORLD color/depth。
session 旗帜的 active、位置、颜色、当前选中状态与短标签按帧复制为只读值；旗杆与旗布共用两份常驻 cuboid GPU 资源，
每面活动旗帜提交两项 WORLD opaque 实例。标签沿用旧路径的 bitmap 字形和连续像素 run 合并，
按旗帜来源槽缓存双面 GPU 字形网格；标签变更时仅替换该槽的网格。字形与旗布共用 Scene WORLD 深度。
活动投射物从玩法数组按 slot 顺序冻结 kind、位置、高度、旋转时间、闪烁状态及 V2 光照；
bomb/molotov 各复用一份 RMESH GPU 资源和模型纹理，闪烁时使用材质纯色，实例变换在 draw 中求值。
其 WORLD opaque draw 与旗帜、地图和角色共用 Scene color/depth；普通帧没有投射物时不加载这两份资源。
session 交互物按来源槽位冻结 kind、weapon、位置、效果高亮及 V2 光照，并在 PLAYING、非暂停、非商店状态下可见。Scene 将七类拾取模型按 primitive 使用常驻 GPU 资源绘制；按钮、药瓶、弹药盒使用共享形体，特殊按钮底座按来源槽位及高度缓存。两类交互物与其余 WORLD opaque 共用目标深度；程序形体仍需固定视觉基线审批。
owner 跨审计帧复用，registry frame pin 在诊断
提交完成后退休，cache 随 generation collect。该诊断不替换 Core mixed executor 的正常提交；
读回耗时也不在此前记录的 `FRAME-AUDIT whole_loop_ms` 内。
固定 normal capture 请求与该审计同时启用时，诊断额外写出同帧 Scene WORLD PPM，
供与 mixed 最终 BMP 核对世界局部画面。

## 角色 GPU skinning

CPU 继续拥有 pose、IK、socket、gear 和 weapon placement。mixed frame 冻结 finalized palette、bind
position/normal、BDEF influence 和索引；compute skinning 输出写入 frame-slot device-local vertex buffer，
body Draw 直接消费。

normal GPU skinning 不生成 CPU reference。`--gpu-character-vertex-diff` 只为目标帧建立对照；
`--gpu-character-skinning-off` 是正式回滚边界，只恢复 CPU-skinned vertex upload，不改变上游所有权。

## Presenter 与失败边界

Windows native presenter 使用同一 Vulkan device/queue 和唯一 swapchain generation。逐帧审计必须保持
fallback、readback、CPU framebuffer copy、hot queue-idle、非法层转换和 poisoned presenter 为零。
`--gpu-required` 下任何 unsupported、preflight、submit、present、readback 或 CPU copy 都必须非零退出。

省略 `--gpu-native-present` 只用于显式的软件呈现 A/B，不是 required runtime 的降级路径。CPU renderer
仍是独立完整实现，用于 reference 和不启用 GPU renderer 的正常运行。

## 支持边界

- Windows 原生 PowerShell、物理 GPU 和 native present 是主开发与签收环境。
- Linux hosted Vulkan、WSL 与 llvmpipe 只用于编译、ABI 或 correctness 辅助诊断，不能替代驱动、窗口、
  resize、presenter 生命周期或性能结论。
- 设备丢失恢复、跨厂商完整矩阵、validation/sync、fault injection 和长时 soak 属于按风险触发的专项，
  不由日常 Quick 自动替代。
- 运行参数以 package 中 `rasterfall.exe --help` 为准；验收范围与证据要求由 GPU 验收指南拥有。
